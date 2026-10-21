/* Exercise the production compiler's interface list and exact key sets. */
#define main shaderaot_entry_for_test
#include "../tools/shaderaot.c"
#undef main
#include <assert.h>
int main(void) {
    Program vertices[21] = {0}, pixel = {0};
    u32 pairs[23];
    for (u32 i = 0; i < 20; ++i) {
        vertices[i].ok = 1;
        vertices[i].outputs = 1u << i;
        pairs[i] = i;
    }
    pairs[20] = 20; pairs[21] = 99; pairs[22] = 0;
    pixel.pairs = pairs; pixel.npairs = 23;
    assert(pixel_masks(&pixel, vertices, 21) == 20);
    assert(pixel.masks[19] == (1u << 19));
    append_pixel_mask(&pixel, 0);
    assert(pixel.nmasks == 21 && pixel.masks[20] == 0);
    append_pixel_mask(&pixel, 1);
    assert(pixel.nmasks == 21);
    KeySet set = {0, 8, 0};
    assert(add_key(&set, SHADER_VERTEX, 0, 0));
    assert(add_key(&set, SHADER_VERTEX, 1, 0));
    assert(add_key(&set, SHADER_PIXEL, 0, 0));
    assert(add_key(&set, SHADER_PIXEL, 0, 1));
    for (u32 i = 0; i < 1000; ++i) assert(add_key(&set, SHADER_PIXEL, i + 2, i % 32));
    assert(set.count == 1004 && set.cap > 8);
    assert(has_key(&set, SHADER_VERTEX, 0, 0) && has_key(&set, SHADER_VERTEX, 1, 0));
    assert(has_key(&set, SHADER_PIXEL, 0, 0) && has_key(&set, SHADER_PIXEL, 0, 1));
    for (u32 i = 0; i < 1000; ++i) assert(has_key(&set, SHADER_PIXEL, i + 2, i % 32));
    assert(!add_key(&set, SHADER_PIXEL, 0, 1));
    assert(!has_key(&set, SHADER_PIXEL, 0, 2));
    free(set.slot); free(pixel.masks);
    puts("all interfaces, low-bit keys, stage separation and growth passed");
    return 0;
}
