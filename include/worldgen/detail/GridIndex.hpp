#pragma once
// ============================================================================
// GridIndex.hpp — in-bounds checks & row-major linear indexing helpers
// For Colony-Game | Windows / MSVC | C++17
//
// Provides:
//   - [[nodiscard]] bool inb(x,y,W,H)
//   - [[nodiscard]] bool inb3(x,y,z,W,H,D)
//   - std::size_t index3(x,y,z,W,H)            // 5-arg (z is a slab index)
//   - std::size_t index3(x,y,z,W,H,D)          // 6-arg with full 3D check
//   - std::size_t index3_clamped(x,y,z,W,H,D)  // clamps coords into bounds
//
// Notes:
//   * Row-major flattening: i = x + y*W + z*W*H
//   * Debug builds assert preconditions for non-clamped variants.
//   * Release builds keep code branch-free and fast.
// ============================================================================

#include <cstddef>    // std::size_t
#include <cassert>    // assert
#include <algorithm>  // std::clamp
#include <type_traits>

namespace worldgen::detail {

// ---- Bounds predicates -------------------------------------------------

// 2D in-bounds check (row-major grids). Marked nodiscard so you don't
// accidentally ignore the result in calling code.
[[nodiscard]] constexpr inline bool inb(int x, int y, int W, int H) noexcept {
    return (x >= 0 && x < W) && (y >= 0 && y < H);
}

// 3D in-bounds check (row-major order: X fastest, then Y, then Z)
[[nodiscard]] constexpr inline bool inb3(int x, int y, int z,
                                         int W, int H, int D) noexcept {
    return inb(x,y,W,H) && (z >= 0 && z < D);
}

// ---- Linear indexers (row-major) --------------------------------------
// Reference formula (row-major):
//    idx = x + y*W + z*W*H
// This generalizes the familiar 2D x + y*W to 3D by adding a z-slab stride.
// (See: Row/column-major order.)  // NOLINT: docs-only
// -----------------------------------------------------------------------

// 5-arg variant used heavily in world-gen code paths where z is a slab id.
// Preconditions (debug-checked): W>0, H>0, inb(x,y,W,H), z>=0.
constexpr inline std::size_t index3(int x, int y, int z,
                                    int W, int H) noexcept {
    assert(W > 0 && H > 0);
    assert(inb(x,y,W,H));
    assert(z >= 0); // caller guarantees valid slab if used

    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(W)
                                     * static_cast<std::size_t>(H);
}

// 6-arg overload with full 3D precondition checks in debug builds.
// D is not needed for the calculation itself but enables assert(z in [0..D)).
constexpr inline std::size_t index3(int x, int y, int z,
                                    int W, int H, int D) noexcept {
    assert(W > 0 && H > 0 && D > 0);
    assert(inb3(x,y,z,W,H,D));

    (void)D; // silence unused warning; not required for row-major flattening
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(W)
                                     * static_cast<std::size_t>(H);
}

// Clamp-to-bounds variant: always returns a valid index, even if any of
// (x,y,z) are outside. Ideal for gradient sampling at edges without branches.
constexpr inline std::size_t index3_clamped(int x, int y, int z,
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
