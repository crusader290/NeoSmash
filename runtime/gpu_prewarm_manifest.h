/* Strict cache basenames: a profile cannot select paths outside the cache. */
#ifndef GPU_PREWARM_MANIFEST_H
#define GPU_PREWARM_MANIFEST_H
#include <stdio.h>
#include <string.h>
static int prewarm_name_valid(const char *name, unsigned wanted_version) {
    unsigned version, mask;
    unsigned long long hash, tables;
    char stage[3], canonical[96];
    int end = 0;
    if (sscanf(name, "v%u_%2[a-z]_%16llx_%8x_%16llx.dxbc%n", &version, stage,
               &hash, &mask, &tables, &end) != 5 || !end || name[end]) return 0;
    if (version != wanted_version || (strcmp(stage, "vs") && strcmp(stage, "ps") && strcmp(stage, "cs"))) return 0;
    snprintf(canonical, sizeof canonical, "v%u_%s_%016llx_%08x_%016llx.dxbc", version, stage, hash, mask, tables);
    return !strcmp(name, canonical);
}
#endif
