/* A direct Dispatch(0,0,0) is discarded by DXVK before pipeline binding.
 * Indirect zero arguments commit the pipeline while launching no invocations.
 * An event query checks completion of those commands, unlike worker CPU idleness.
 * No game resources are bound or modified here (renderer initialization only). */
static void prewarm_compute_pipelines(void) {
    unsigned count = 0;
    for (u32 i = 0; i < PREWARM_SLOTS; ++i)
        count += prewarm_table[i].stage == SHADER_COMPUTE && prewarm_table[i].state == PW_READY;
    if (!count) return;
    ID3D11Buffer *arguments = NULL;
    ID3D11Query *completed = NULL;
    UINT zero[3] = {0, 0, 0};
    D3D11_BUFFER_DESC bd = {sizeof zero, D3D11_USAGE_IMMUTABLE, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0};
    D3D11_SUBRESOURCE_DATA data = {zero, 0, 0};
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, &data, &arguments)) ||
        FAILED(ID3D11Device_CreateQuery(dev, &qd, &completed))) {
        fprintf(stderr, "[gfx] compute preparation: resource creation FAILED\n");
        if (arguments) ID3D11Buffer_Release(arguments);
        return;
    }
    u64 start = GetTickCount64();
    for (u32 i = 0; i < PREWARM_SLOTS; ++i) {
        Prewarm *p = &prewarm_table[i];
        if (p->stage != SHADER_COMPUTE || p->state != PW_READY) continue;
        ID3D11DeviceContext_CSSetShader(ctx, (ID3D11ComputeShader *)p->obj, NULL, 0);
        ID3D11DeviceContext_DispatchIndirect(ctx, arguments, 0);
    }
    ID3D11DeviceContext_CSSetShader(ctx, NULL, NULL, 0);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)completed);
    ID3D11DeviceContext_Flush(ctx);
    HRESULT result;
    BOOL done = FALSE;
    do {
        result = ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)completed, &done, sizeof done, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(result) || (result == S_OK && done)) break;
        Sleep(10);
    } while (GetTickCount64() - start < 180000);
    if (result == S_OK && done)
        fprintf(stderr, "[gfx] compute preparation: %u zero-work indirect pipelines completed on GPU in %.2f s; resource-dependent specialization may remain\n",
                count, (GetTickCount64() - start) / 1000.0);
    else
        fprintf(stderr, "[gfx] compute preparation: INCOMPLETE, result %#lx, %.1f s\n", (unsigned long)result, (GetTickCount64() - start) / 1000.0);
    ID3D11Query_Release(completed);
    ID3D11Buffer_Release(arguments);
}
