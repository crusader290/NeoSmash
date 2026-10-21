/* Native warm-up: `ssbu --native-warm <list> <cache dir>` (gpu_d3d11.c; host.c's main).
 *
 * Under DXVK, a shader's Vulkan pipeline library is compiled by the GPU driver, which keeps the
 * result in a disk cache of this executable's own (AMD's is keyed by the program's path: a copy
 * elsewhere starts empty). Measured on this PC: a shader the driver has never compiled costs
 * 20-850 ms at its first draw, on DXVK's command thread, so the next Present waits that long; one
 * it has compiled before costs under 5 ms. Compiling DXBC ahead of time does not help with that.
 *
 * This mode creates every listed DXBC file on a device of its own (no window, no game, no save),
 * waits for DXVK's workers to compile the libraries, and exits. tools/warm_native_cache.py runs it
 * in batches over the whole-game inventory: each process keeps every library it made. */

typedef struct { const char *dir; char (*names)[96]; u32 n; volatile LONG next, made, failed; ID3D11Device *d; } NativeWarm;

static DWORD WINAPI native_warm_thread(void *arg) {
    NativeWarm *w = arg;
    for (LONG i; (i = InterlockedIncrement(&w->next) - 1) < (LONG)w->n;) {
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", w->dir, w->names[i]);
        FILE *f = fopen(path, "rb");
        void *code = NULL;
        long size = 0;
        if (f && !fseek(f, 0, SEEK_END) && (size = ftell(f)) > 4 && !fseek(f, 0, SEEK_SET) && (code = malloc(size)) &&
            fread(code, 1, size, f) != (size_t)size) size = 0;
        if (f) fclose(f);
        int stage = !strncmp(w->names[i] + 4, "vs", 2) ? SHADER_VERTEX : !strncmp(w->names[i] + 4, "ps", 2) ? SHADER_PIXEL : SHADER_COMPUTE;
        void *obj = NULL;
        /* (released at once: DXVK's pipeline manager keeps the shader and its library) */
        if (code && size > 4 && !memcmp(code, "DXBC", 4) && SUCCEEDED(create_on(w->d, stage, code, (size_t)size, &obj))) {
            IUnknown_Release((IUnknown *)obj);
            InterlockedIncrement(&w->made);
        } else {
            InterlockedIncrement(&w->failed);
            fprintf(stderr, "[warm] cannot create %s\n", w->names[i]);
        }
        free(code);
    }
    return 0;
}

int gfx_native_warm_main(const char *list, const char *dir) {
    if (!on_dxvk()) { fprintf(stderr, "[warm] d3d11.dll is not DXVK: nothing to warm\n"); return 2; }
    NativeWarm w = {dir};
    FILE *file = fopen(list, "r");
    if (!file) { fprintf(stderr, "[warm] cannot open %s\n", list); return 2; }
    char line[256];
    u32 cap = 0;
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (!prewarm_name_valid(line, SHADER_CACHE_VERSION)) { fprintf(stderr, "[warm] invalid entry: %.96s\n", line); fclose(file); return 2; }
        if (w.n == cap && !(w.names = realloc(w.names, sizeof *w.names * (cap = cap * 2 + 1024)))) { fclose(file); return 2; }
        snprintf(w.names[w.n++], sizeof *w.names, "%s", line);
    }
    fclose(file);
    if (!w.n) { fprintf(stderr, "[warm] empty list %s\n", list); return 2; }

    /* the game's own device: the same flags, so DXVK makes the same libraries */
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (getenv("SSBU_D3D_DRIVER_THREADS") && !atoi(getenv("SSBU_D3D_DRIVER_THREADS"))) flags |= D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, 2, D3D11_SDK_VERSION, &w.d, NULL, NULL))) {
        fprintf(stderr, "[warm] cannot create a Direct3D 11 device\n");
        return 2;
    }
    u64 start = GetTickCount64(), bytes0 = private_bytes();
    fprintf(stderr, "[warm] %u shaders from %s\n", w.n, list);
    HANDLE threads[8];
    for (u32 t = 0; t < 8; t++) threads[t] = CreateThread(NULL, 0, native_warm_thread, &w, 0, NULL);
    for (u32 t = 0; t < 8; t++) { WaitForSingleObject(threads[t], INFINITE); CloseHandle(threads[t]); }
    fprintf(stderr, "[warm] %ld created, %ld failed in %.1f s; private memory %.0f -> %.0f MB\n", (long)w.made, (long)w.failed,
            (GetTickCount64() - start) / 1000.0, bytes0 / 1048576.0, private_bytes() / 1048576.0);
    int quiet = prewarm_wait_driver();
    fprintf(stderr, "[warm] %s after %.1f s; private memory %.0f MB\n", quiet ? "done" : "INCOMPLETE (workers did not go quiet)",
            (GetTickCount64() - start) / 1000.0, private_bytes() / 1048576.0);
    free(w.names);
    return w.failed ? 1 : quiet ? 0 : 3;
}
