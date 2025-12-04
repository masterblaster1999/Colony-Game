#pragma once
#include <cstddef>
#include <algorithm>
#include <type_traits>

namespace worldgen::detail {

// Flatten (x,y) into a 1D index with row stride `width`.
constexpr std::size_t index3(int x, int y, int width) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
         + static_cast<std::size_t>(x);
}

} // namespace worldgen::detail
