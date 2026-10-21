/* Compute ownership/readback decisions, separate from D3D resource operations. */
#ifndef GPU_COMPUTE_POLICY_H
#define GPU_COMPUTE_POLICY_H
static unsigned compute_write_mask(unsigned bindings, unsigned observed, int eager) {
    if (!eager) return observed;
    return bindings >= 16 ? 0xffffu : (1u << bindings) - 1u;
}
static int compute_readback_needed(int stores, int learned, unsigned fallback, unsigned slot) {
    return stores && (!learned || (slot < 16 && (fallback >> slot & 1u)));
}
#endif
