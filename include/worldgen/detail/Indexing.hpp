// include/worldgen/detail/Indexing.hpp  (NEW)
#pragma once
#include <cstddef>
#include <algorithm>
#include <type_traits>

namespace worldgen::detail {

// Flatten (x,y) into linear index given sizeX.
constexpr inline std::size_t index2(int x, int y, int sizeX) noexcept {
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(sizeX);
}

// Flatten (x,y,z) into linear index given sizeX, sizeY. (sizeZ not needed)
constexpr inline std::size_t index3(int x, int y, int z, int sizeX, int sizeY) noexcept {
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(sizeX)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(sizeX) * static_cast<std::size_t>(sizeY);
}

// Optional 6-arg overload for callsites that pass all three dimensions.
constexpr inline std::size_t index3(int x, int y, int z, int sizeX, int sizeY, int /*sizeZ*/) noexcept {
    return index3(x, y, z, sizeX, sizeY); // sizeZ not needed for flattening
}

// Clamp helpers if you sample neighbors (x±1 etc.) and want safe edges.
constexpr inline int clampi(int v, int lo, int hi) noexcept { return std::max(lo, std::min(v, hi)); }

// Safe variant that clamps coords to valid range [0..size-1]
constexpr inline std::size_t index3_clamped(int x, int y, int z, int sizeX, int sizeY, int sizeZ) noexcept {
    x = clampi(x, 0, sizeX - 1);
    y = clampi(y, 0, sizeY - 1);
    z = clampi(z, 0, sizeZ - 1);
    return index3(x, y, z, sizeX, sizeY);
}

} // namespace worldgen::detail
