/* The disk cache of compiled shaders (DXBC files), shared by the renderer (gpu_d3d11.c) and the
 * ahead-of-time compiler (tools/shaderaot.c), so that both key, name and compile a program alike.
 *
 * A file is "<dir>/v<version>_<vs|ps|cs>_<program hash>_<vs outputs>_<jump tables' hash>.dxbc".
 * The ahead-of-time compiler dates its files 2001-01-01 until the game first uses them (the
 * renderer then dates them now): the start's prewarm makes only shaders the game has used. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>

#include "gpu_shader.h"

/* Bump when the translator changes what it generates: older files are then ignored. */
#define SHADER_CACHE_VERSION 10
#define SHADER_CACHE_UNPLAYED 0x01C07385C89DC000ull   /* FILETIME 2001-01-01 */
#define SHADER_CACHE_PLAYED   0x01C1925741648000ull   /* written after 2002-01-01: the game used it */

/* SPH v3: type/stage must agree, and the pixel output word has only sample-mask/depth
 * flags. The other 30 bits are reserved (NVIDIA's Shader Program Header specification).
 * Archive scans can otherwise mistake compressed/texture data for shader programs. */
static inline int shader_program_header_valid(const u8 *program, u32 size) {
    if (size < 0x50) return 0;
    u32 first, last;
    memcpy(&first, program, sizeof first);
    memcpy(&last, program + 0x4C, sizeof last);
    u32 type = first & 31, version = (first >> 5) & 31, stage = (first >> 10) & 15, sass = (first >> 17) & 15;
    if (version != 3 || sass != 3) return 0;
    u32 word3, word4, word5;
    memcpy(&word3, program + 12, sizeof word3);
    memcpy(&word4, program + 16, sizeof word4);
    memcpy(&word5, program + 20, sizeof word5);
    /* Common words / ImapSystemValuesA reserved fields. Bit 25 is used by
     * valid Maxwell programs in the game and must remain accepted. */
    if ((first & 0x01E00000u) || (word3 & 0xF0000000u) ||
        (word4 & 0x00F00000u) || (word5 & 0x00FFFC0Fu)) return 0;
    return type == 1 && stage == 1 ? 1 : type == 2 && stage == 5 && !(last & 0xFFFFFFFCu);
}

/* FNV-1a over a program's bytes (a vertex or pixel program: its header and code). */
static inline u64 shader_program_hash(const u8 *p, u32 size) {
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < size; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static inline void shader_cache_file(char *out, size_t n, const char *dir, int stage, u64 hash, u32 vs_outputs, u64 table_hash) {
    snprintf(out, n, "%s/v%u_%s_%016llx_%08x_%016llx.dxbc", dir, SHADER_CACHE_VERSION,
             stage == SHADER_VERTEX ? "vs" : stage == SHADER_PIXEL ? "ps" : "cs", (unsigned long long)hash, vs_outputs,
             (unsigned long long)table_hash);
}

/* fxc as the renderer runs it: dispatch-loop programs ("npc_") without fxc's optimiser (they
 * compile much faster, and the driver optimises the bytecode anyway); any other program again
 * without it when the optimiser fails (it has internal errors on some). */
static inline HRESULT shader_cache_compile(const char *hlsl, const char *name, int stage, ID3DBlob **code, ID3DBlob **errors) {
    const char *target = stage == SHADER_VERTEX ? "vs_5_0" : stage == SHADER_PIXEL ? "ps_5_0" : "cs_5_0";
    UINT flags = strstr(hlsl, "npc_") ? D3DCOMPILE_SKIP_OPTIMIZATION : D3DCOMPILE_OPTIMIZATION_LEVEL1;
    *code = *errors = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL, "main", target, flags, 0, code, errors);
    if (FAILED(hr) && flags != D3DCOMPILE_SKIP_OPTIMIZATION) {
        if (*errors) (*errors)->lpVtbl->Release(*errors);
        *errors = NULL;
        hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL, "main", target, D3DCOMPILE_SKIP_OPTIMIZATION, 0, code, errors);
    }
    return hr;
}
