/* Manual GPU integration test: zero-work preparation must not execute the shader.
 * Run beside the experimental DXVK DLLs, linked with d3d11/d3dcompiler. */
#define COBJMACROS
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <d3d11.h>
#include <d3dcompiler.h>
typedef uint64_t u64;
typedef uint32_t u32;
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
enum { SHADER_COMPUTE = 2, PW_READY = 3 };
#define PREWARM_SLOTS 1
typedef struct { int stage, state; void *obj; } Prewarm;
static Prewarm prewarm_table[1];
#include "gpu_prewarm_compute.h"

#define CHECK(call) do { HRESULT hr = (call); if (FAILED(hr)) { fprintf(stderr, "line %d: %#lx\n", __LINE__, (unsigned long)hr); return 1; } } while (0)
static int counter(ID3D11Buffer *source, ID3D11Buffer *staging, UINT wanted) {
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)source);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped))) return 0;
    UINT value = *(UINT *)mapped.pData;
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    fprintf(stderr, "counter %u, expected %u\n", value, wanted);
    return value == wanted;
}
int main(void) {
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    CHECK(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &level, 1, D3D11_SDK_VERSION, &dev, NULL, &ctx));
    const char *text = "RWByteAddressBuffer b:register(u0); [numthreads(1,1,1)] void main(uint3 i:SV_DispatchThreadID) { uint x; b.InterlockedAdd(0,1,x); }";
    ID3DBlob *code = NULL, *errors = NULL;
    CHECK(D3DCompile(text, strlen(text), "zero-work-test", NULL, NULL, "main", "cs_5_0", 0, 0, &code, &errors));
    ID3D11ComputeShader *shader = NULL;
    CHECK(ID3D11Device_CreateComputeShader(dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &shader));
    UINT zero[4] = {0};
    D3D11_SUBRESOURCE_DATA data = {zero, 0, 0};
    D3D11_BUFFER_DESC bd = {sizeof zero, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
    ID3D11Buffer *output = NULL, *staging = NULL;
    CHECK(ID3D11Device_CreateBuffer(dev, &bd, &data, &output));
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(ID3D11Device_CreateBuffer(dev, &bd, NULL, &staging));
    D3D11_UNORDERED_ACCESS_VIEW_DESC view = {0};
    view.Format = DXGI_FORMAT_R32_TYPELESS; view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = 4; view.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ID3D11UnorderedAccessView *uav = NULL, *none = NULL;
    CHECK(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)output, &view, &uav));
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &uav, NULL);
    prewarm_table[0] = (Prewarm){SHADER_COMPUTE, PW_READY, shader};
    prewarm_compute_pipelines();
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &none, NULL);
    if (!counter(output, staging, 0)) return 2;
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &uav, NULL);
    ID3D11DeviceContext_CSSetShader(ctx, shader, NULL, 0);
    ID3D11DeviceContext_Dispatch(ctx, 1, 1, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &none, NULL);
    if (!counter(output, staging, 1)) return 3;
    fprintf(stderr, "PASS: zero indirect preparation did no work; positive dispatch wrote once\n");
    return 0;
}
