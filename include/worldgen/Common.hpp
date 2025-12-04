#pragma once
#include <cstddef>
#include <algorithm>

namespace worldgen::detail {

// 2D -> 1D index
constexpr inline std::size_t index3(int x, int y, int W) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
         + static_cast<std::size_t>(x);
}

// In-bounds test
constexpr inline bool inb(int x, int y, int W, int H) noexcept {
    return static_cast<unsigned>(x) < static_cast<unsigned>(W) &&
           static_cast<unsigned>(y) < static_cast<unsigned>(H);
}

// Clamp to [0,1]
constexpr inline float clamp01(float v) noexcept {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

} // namespace worldgen::detail
