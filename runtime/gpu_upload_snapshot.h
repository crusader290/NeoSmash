/* Exact CPU bytes plus a conservative generation for all GPU-resident data.
 * Large uploads bypass snapshots to bound retained CPU memory per binding slot. */
#ifndef GPU_UPLOAD_SNAPSHOT_H
#define GPU_UPLOAD_SNAPSHOT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define GPU_SNAPSHOT_MAX 65536u
typedef struct GpuUploadSnapshot {
    uint64_t address, generation;
    uint32_t size, capacity;
    unsigned char *bytes;
} GpuUploadSnapshot;
static int gpu_snapshot_matches(const GpuUploadSnapshot *s, uint64_t address, uint32_t size,
                                uint64_t generation, const void *bytes) {
    return size && size <= GPU_SNAPSHOT_MAX && s->bytes && s->size == size &&
           s->address == address && s->generation == generation && !memcmp(s->bytes, bytes, size);
}
static void gpu_snapshot_commit(GpuUploadSnapshot *s, uint64_t address, uint32_t size,
                                uint64_t generation, const void *bytes) {
    s->size = 0;  /* every failed/bypassed commit invalidates the previous snapshot */
    if (!size || size > GPU_SNAPSHOT_MAX) return;
    if (s->capacity < size) {
        unsigned char *grown = realloc(s->bytes, size);
        if (!grown) return;
        s->bytes = grown;
        s->capacity = size;
    }
    memcpy(s->bytes, bytes, size);
    s->address = address;
    s->generation = generation;
    s->size = size;
}
#endif
