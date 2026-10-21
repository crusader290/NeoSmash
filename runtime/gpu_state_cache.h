/* Exact descriptor caches: a recent hit avoids hashing; other hits use an index.
 * Hash collisions are always resolved by comparing the complete description.
 * The 512-object limit and uncached overflow behavior match the original cache. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef GPU_STATE_CACHE_LINEAR
#define GPU_STATE_CACHE_LINEAR 0
#endif

static inline uint32_t gpu_state_hash(const void *description, size_t bytes) {
    const unsigned char *p = description;
    uint64_t h = 0x9e3779b97f4a7c15ull;
    while (bytes >= 4) {
        uint32_t word;
        memcpy(&word, p, 4);
        h = (h ^ word) * 0x100000001b3ull;
        p += 4; bytes -= 4;
    }
    while (bytes--) h = (h ^ *p++) * 0x100000001b3ull;
    h ^= h >> 32;
    return (uint32_t)h;
}

#define GPU_STATE_CACHE(Name, Desc, Iface, Create)                                    \
    static struct { Desc desc; Iface *obj; } Name##_cache[512];                       \
    static uint16_t Name##_index[1024];                                              \
    static uint32_t Name##_count, Name##_last, Name##_previous;                       \
    static Iface *Name(const Desc *d) {                                              \
        uint32_t bucket = 0;                                                        \
        int indexed = !GPU_STATE_CACHE_LINEAR && Name##_count > 32;                  \
        if (indexed) {                                                              \
            if (!memcmp(&Name##_cache[Name##_last].desc, d, sizeof *d))               \
                return Name##_cache[Name##_last].obj;                                \
            if (Name##_count > 1 &&                                                 \
                !memcmp(&Name##_cache[Name##_previous].desc, d, sizeof *d)) {         \
                uint32_t i = Name##_previous; Name##_previous = Name##_last;          \
                Name##_last = i; return Name##_cache[i].obj;                         \
            }                                                                       \
        }                                                                           \
        if (!indexed) {                                                             \
            for (uint32_t i = 0; i < Name##_count; i++)                              \
                if (!memcmp(&Name##_cache[i].desc, d, sizeof *d))                    \
                    return Name##_cache[i].obj;                                     \
        } else {                                                                    \
            bucket = gpu_state_hash(d, sizeof *d) & 1023;                            \
            while (Name##_index[bucket]) {                                          \
                uint32_t i = Name##_index[bucket] - 1;                               \
                if (!memcmp(&Name##_cache[i].desc, d, sizeof *d)) {                  \
                    Name##_previous = Name##_last; Name##_last = i;                 \
                    return Name##_cache[i].obj;                                     \
                }                                                                   \
                bucket = (bucket + 1) & 1023;                                       \
            }                                                                       \
        }                                                                           \
        Iface *o = NULL;                                                            \
        Create(dev, d, &o);                                                         \
        if (Name##_count < 512) {                                                   \
            if (!indexed) {                                                         \
                bucket = gpu_state_hash(d, sizeof *d) & 1023;                        \
                while (Name##_index[bucket]) bucket = (bucket + 1) & 1023;            \
            }                                                                       \
            uint32_t i = Name##_count++;                                            \
            Name##_cache[i].desc = *d; Name##_cache[i].obj = o;                       \
            Name##_index[bucket] = (uint16_t)(i + 1);                                \
            Name##_previous = Name##_last; Name##_last = i;                          \
        }                                                                           \
        return o;                                                                   \
    }
