/* DXVK exposes no public library-compilation completion fence through D3D11.
 * Worker CPU quiescence is an explicitly labelled heuristic, never a GPU-ready fence. */
#ifdef _WIN32
#include <tlhelp32.h>
static int prewarm_dxvk_activity(u64 *cpu, u64 *identity, unsigned *count) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry = {sizeof entry};
    *cpu = *identity = 0; *count = 0;
    int ok = 1;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) { ok = 0; continue; }
        PWSTR name = NULL;
        if (SUCCEEDED(GetThreadDescription(thread, &name)) && name && !wcsncmp(name, L"dxvk-shader", 11)) {
            FILETIME created, exited, kernel, user;
            if (!GetThreadTimes(thread, &created, &exited, &kernel, &user)) ok = 0;
            else {
                *cpu += ((u64)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime)
                      + ((u64)user.dwHighDateTime << 32 | user.dwLowDateTime);
                *identity += entry.th32ThreadID;
                ++*count;
            }
        }
        if (name) LocalFree(name);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return ok && *count;
}
#endif

/* Both preparation phases honor the launcher's explicit limit. */
static int prewarm_wait_seconds(void) {
    const char *setting = getenv("SSBU_EFFECT_WAIT_SECONDS");
    int seconds = setting ? atoi(setting) : 180;
    return seconds < 3 ? 3 : seconds > 3600 ? 3600 : seconds;
}

/* 1 once the workers went quiet; 0 on timeout, or where that cannot be observed */
static int prewarm_wait_driver(void) {
#ifdef _WIN32
    u64 start = GetTickCount64(), last_log = start, idle_since = 0, previous = 0, previous_ids = 0;
    unsigned previous_count = 0;
    int seconds = prewarm_wait_seconds();
    fprintf(stderr, "[gfx] effect preparation: waiting for DXVK shader-worker CPU quiescence (heuristic, %d s limit)\n", seconds);
    while (GetTickCount64() - start < (u64)seconds * 1000) {
        u64 cpu, ids, now = GetTickCount64();
        unsigned count;
        int observed = prewarm_dxvk_activity(&cpu, &ids, &count);
        if (observed && count == previous_count && ids == previous_ids && cpu >= previous && cpu - previous <= 10000) {
            if (!idle_since) idle_since = now;
            if (now - idle_since >= 3000) {
                fprintf(stderr, "[gfx] effect preparation: %u DXVK shader workers quiet for 3 s after %.1f s; pipeline first-use work may remain\n",
                        count, (now - start) / 1000.0);
                return 1;
            }
        } else idle_since = 0;
        previous = observed ? cpu : 0; previous_ids = observed ? ids : 0; previous_count = observed ? count : 0;
        if (now - last_log >= 10000) {
            fprintf(stderr, "[gfx] effect preparation: still waiting, %.0f s, %u observed shader workers\n", (now - start) / 1000.0, previous_count);
            last_log = now;
        }
        Sleep(250);
    }
    fprintf(stderr, "[gfx] effect preparation: TIMEOUT; driver completion unverified, continuing without a readiness guarantee\n");
    return 0;
#else
    fprintf(stderr, "[gfx] effect preparation: driver completion observation unavailable on this platform\n");
    return 0;
#endif
}
