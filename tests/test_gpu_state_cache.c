/* Native correctness and lookup-cost check; no GPU, game assets, or driver calls. */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int linear_mode;
#define GPU_STATE_CACHE_LINEAR linear_mode
#include "gpu_state_cache.h"

typedef struct Object { D3D11_BLEND_DESC desc; } Object;
static Object objects[8192];
static unsigned made;
static void *dev;
static void create(void *unused, const D3D11_BLEND_DESC *d, Object **out) {
    (void)unused;
    assert(made < 8192);
    objects[made].desc = *d;
    *out = &objects[made++];
}
GPU_STATE_CACHE(lookup, D3D11_BLEND_DESC, Object, create)
GPU_STATE_CACHE(bench_linear, D3D11_BLEND_DESC, Object, create)
GPU_STATE_CACHE(bench_index, D3D11_BLEND_DESC, Object, create)
static void create_depth(void *unused, const D3D11_DEPTH_STENCIL_DESC *d, Object **out) {
    (void)unused;
    assert(made < 8192);
    memset(&objects[made], 0, sizeof objects[made]);
    memcpy(&objects[made].desc, d, sizeof *d);
    *out = &objects[made++];
}
GPU_STATE_CACHE(depth_linear, D3D11_DEPTH_STENCIL_DESC, Object, create_depth)
GPU_STATE_CACHE(depth_index, D3D11_DEPTH_STENCIL_DESC, Object, create_depth)

static D3D11_BLEND_DESC description(unsigned id) {
    D3D11_BLEND_DESC d = {0};
    d.IndependentBlendEnable = 1;
    /* Exercise equality beyond the first render target and the first cache line. */
    d.RenderTarget[7].RenderTargetWriteMask = id & 15;
    d.RenderTarget[6].SrcBlend = (D3D11_BLEND)(id >> 4);
    return d;
}
static void correctness(void) {
    Object *expected[512];
    unsigned buckets[1024] = {0}, collisions = 0;
    for (unsigned i = 0; i < 512; i++) {
        D3D11_BLEND_DESC d = description(i);
        unsigned bucket = gpu_state_hash(&d, sizeof d) & 1023;
        collisions += buckets[bucket]++ != 0;
        expected[i] = lookup(&d);
        assert(expected[i] && !memcmp(&expected[i]->desc, &d, sizeof d));
    }
    assert(collisions > 0);
    unsigned before = made, rng = 17;
    for (unsigned i = 0; i < 100000; i++) {
        rng = rng * 1664525u + 1013904223u;
        unsigned key = rng >> 16 & 511;
        D3D11_BLEND_DESC d = description(key);
        assert(lookup(&d) == expected[key]);
    }
    assert(made == before);
    for (unsigned i = 512; i < 530; i++) {
        D3D11_BLEND_DESC d = description(i);
        Object *a = lookup(&d), *b = lookup(&d);
        assert(a != b && !memcmp(&a->desc, &d, sizeof d) && !memcmp(&b->desc, &d, sizeof d));
    }
    for (unsigned i = 0; i < 512; i++) {
        D3D11_BLEND_DESC d = description(i);
        assert(lookup(&d) == expected[i]);
    }
    /* Diagnostic linear mode and indexed mode must share identical entries. */
    linear_mode = 1;
    for (unsigned i = 0; i < 512; i++) { D3D11_BLEND_DESC d = description(i); assert(lookup(&d) == expected[i]); }
    linear_mode = 0;
    printf("correctness: collisions=%u, 100000 mixed hits, capacity/overflow and mode switch passed\n", collisions);
}
static volatile uintptr_t checksum;
static double bench_depth(unsigned repeat, int linear) {
    D3D11_DEPTH_STENCIL_DESC d[37] = {0};
    linear_mode = linear;
    for (unsigned i = 0; i < 37; i++) {
        d[i].BackFace.StencilFunc = (D3D11_COMPARISON_FUNC)(i + 1);
        Object *o = linear ? depth_linear(&d[i]) : depth_index(&d[i]);
        assert(o && !memcmp(&o->desc, &d[i], sizeof d[i]));
    }
    LARGE_INTEGER start, end, frequency; QueryPerformanceFrequency(&frequency);
    uintptr_t sum = 0;
    const unsigned iterations = 1000000;
    QueryPerformanceCounter(&start);
    for (unsigned i = 0; i < iterations; i++) {
        unsigned key = i / repeat % 37;
        sum += (uintptr_t)(linear ? depth_linear(&d[key]) : depth_index(&d[key]));
    }
    QueryPerformanceCounter(&end); checksum = sum;
    return (end.QuadPart - start.QuadPart) * 1e9 / frequency.QuadPart / iterations;
}
static double bench(Object *(*get)(const D3D11_BLEND_DESC *), unsigned count, unsigned repeat, int linear) {
    D3D11_BLEND_DESC d[512];
    linear_mode = linear;
    for (unsigned i = 0; i < count; i++) { d[i] = description(i); get(&d[i]); }
    LARGE_INTEGER start, end, frequency; QueryPerformanceFrequency(&frequency);
    uintptr_t sum = 0;
    const unsigned iterations = 1000000;
    QueryPerformanceCounter(&start);
    for (unsigned i = 0; i < iterations; i++) sum += (uintptr_t)get(&d[(i / repeat) % count]);
    QueryPerformanceCounter(&end); checksum = sum;
    return (end.QuadPart - start.QuadPart) * 1e9 / frequency.QuadPart / iterations;
}
int main(int argc, char **argv) {
    correctness();
    if (argc > 1 && !strcmp(argv[1], "--check")) return 0;
    for (unsigned count = 2; count <= 128; count *= 8) {
        for (unsigned repeat = 1; repeat <= 8; repeat *= 8) {
            double old_ns = bench(bench_linear, count, repeat, 1);
            double new_ns = bench(bench_index, count, repeat, 0);
            printf("benchmark: states=%u repeat=%u linear=%.2fns indexed=%.2fns ratio=%.3f\n", count, repeat, old_ns, new_ns, new_ns / old_ns);
        }
    }
    for (unsigned repeat = 1; repeat <= 8; repeat *= 8) {
        double old_ns = bench_depth(repeat, 1), new_ns = bench_depth(repeat, 0);
        printf("benchmark depth: states=37 repeat=%u linear=%.2fns indexed=%.2fns ratio=%.3f\n", repeat, old_ns, new_ns, new_ns / old_ns);
    }
    return 0;
}
