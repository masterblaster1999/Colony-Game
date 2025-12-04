#pragma once
// ============================================================================
// GridIndex.hpp — in-bounds checks & row-major linear indexing helpers
// For Colony-Game | Windows / MSVC | C++17
//
// Provides:
//   - [[nodiscard]] bool inb(x,y,W,H)
//   - [[nodiscard]] bool inb3(x,y,z,W,H,D)
//   - std::size_t index2(x,y,W)
//   - std::size_t index3(x,y,z,W,H)            // 5-arg (z is a slab index)
//   - std::size_t index3(x,y,z,W,H,D)          // 6-arg (asserts full 3D)
//   - std::size_t index3_clamped(x,y,z,W,H,D)  // clamps coords into bounds
//
// Notes:
//   * Row-major flattening: idx = x + y*W + z*W*H
//   * Debug builds assert preconditions for non-clamped variants.
//   * Kept non-constexpr to avoid C2475/C3615 interactions on MSVC.
// ============================================================================

#include <cstddef>    // std::size_t
#include <cassert>    // assert
#include <algorithm>  // std::clamp

namespace worldgen::detail {

// ---- Bounds predicates -------------------------------------------------

[[nodiscard]] inline bool inb(int x, int y, int W, int H) noexcept {
    return (x >= 0 && x < W) && (y >= 0 && y < H);
}

[[nodiscard]] inline bool inb3(int x, int y, int z,
                               int W, int H, int D) noexcept {
    return inb(x,y,W,H) && (z >= 0 && z < D);
}

// ---- Linear indexers (row-major) --------------------------------------
// Reference: idx = x + y*W + z*W*H
// -----------------------------------------------------------------------

inline std::size_t index2(int x, int y, int W) noexcept {
    assert(W > 0);
    assert(x >= 0 && x < W);
    assert(y >= 0);
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W);
}

// 5-arg variant: z is a slab id; D isn't required for the math.
inline std::size_t index3(int x, int y, int z,
                          int W, int H) noexcept {
    assert(W > 0 && H > 0);
    assert(inb(x,y,W,H));
    assert(z >= 0);
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(W)
                                     * static_cast<std::size_t>(H);
}

// 6-arg overload with full 3D precondition checks in debug builds.
inline std::size_t index3(int x, int y, int z,
                          int W, int H, int D) noexcept {
    assert(W > 0 && H > 0 && D > 0);
    assert(inb3(x,y,z,W,H,D));
    (void)D; // for symmetric signature; not needed for the calculation
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(W)
                                     * static_cast<std::size_t>(H);
}

// Clamp-to-bounds variant: always returns a valid index, even if (x,y,z)
// are outside. Ideal for gradient sampling at edges without branches.
inline std::size_t index3_clamped(int x, int y, int z,
                                  int W, int H, int D) noexcept {
    assert(W > 0 && H > 0 && D > 0);

    x = std::clamp(x, 0, W - 1);
    y = std::clamp(y, 0, H - 1);
    z = std::clamp(z, 0, D - 1);

    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(W)
                                     * static_cast<std::size_t>(H);
}

} // namespace worldgen::detail
