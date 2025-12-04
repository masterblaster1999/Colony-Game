// include/worldgen/detail/helpers.hpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace worldgen::detail {

// 0..1 clamp for common scalar types
inline constexpr float  clamp01(float  v) noexcept { return v < 0.f  ? 0.f  : (v > 1.f  ? 1.f  : v); }
inline constexpr double clamp01(double v) noexcept { return v < 0.0  ? 0.0  : (v > 1.0  ? 1.0  : v); }

// 2D/3D indexers and in-bounds checks
inline constexpr size_t index2(int x, int y, int W) noexcept {
    return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
}
inline constexpr size_t index3(int x, int y, int z, int W, int H) noexcept {
    return (static_cast<size_t>(z) * static_cast<size_t>(H)
          + static_cast<size_t>(y)) * static_cast<size_t>(W)
          + static_cast<size_t>(x);
}
inline constexpr bool inb(int x, int y, int W, int H) noexcept {
    return static_cast<unsigned>(x) < static_cast<unsigned>(W)
        && static_cast<unsigned>(y) < static_cast<unsigned>(H);
}
inline constexpr bool inb(int x, int y, int z, int W, int H, int D) noexcept {
    return static_cast<unsigned>(x) < static_cast<unsigned>(W)
        && static_cast<unsigned>(y) < static_cast<unsigned>(H)
        && static_cast<unsigned>(z) < static_cast<unsigned>(D);
}

// Backward-compat shim if callers use `I(...)` for 2D
inline constexpr size_t I(int x, int y, int W) noexcept { return index2(x, y, W); }

} // namespace worldgen::detail
