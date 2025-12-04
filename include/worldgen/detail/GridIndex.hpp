// include/worldgen/detail/GridIndex.hpp
#pragma once
#include <cstddef>
#include <array>
#include <type_traits>

namespace worldgen::detail {

// --- in-bounds checks --------------------------------------------------------

template <class T>
[[nodiscard]] constexpr bool inb(T v, T lo, T hi) noexcept {
    static_assert(std::is_arithmetic_v<T>, "inb requires arithmetic type");
    return v >= lo && v < hi;
}

// Common short form: [0, hi)
template <class T>
[[nodiscard]] constexpr bool inb(T v, T hi) noexcept {
    static_assert(std::is_arithmetic_v<T>, "inb requires arithmetic type");
    return v >= T{0} && v < hi;
}

// 2D overload (x in [0,w), y in [0,h))
template <class T>
[[nodiscard]] constexpr bool inb(T x, T y, T w, T h) noexcept {
    return inb(x, w) && inb(y, h);
}

// 3D overload (x in [0,sx), y in [0,sy), z in [0,sz))
template <class T>
[[nodiscard]] constexpr bool inb(T x, T y, T z, T sx, T sy, T sz) noexcept {
    return inb(x, sx) && inb(y, sy) && inb(z, sz);
}

// --- 3D->1D indexing ---------------------------------------------------------

// Row-major: fastest-changing X, then Y, then Z.
// Layout = (z * sy + y) * sx + x
[[nodiscard]] constexpr std::size_t
index3(std::size_t x, std::size_t y, std::size_t z,
       std::size_t sx, std::size_t sy, std::size_t /*sz*/) noexcept
{
    // caller must ensure inb(x,y,z,sx,sy,sz)
    return (z * sy + y) * sx + x;
}

[[nodiscard]] constexpr std::size_t
index3(std::array<std::size_t,3> p, std::array<std::size_t,3> s) noexcept
{
    return index3(p[0], p[1], p[2], s[0], s[1], s[2]);
}

} // namespace worldgen::detail
