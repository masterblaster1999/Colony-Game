#pragma once
#include <cstddef>
#include <type_traits>

namespace worldgen::detail {

struct Extent3 {
    int x{}, y{}, z{};
};

// Canonical 3D -> 1D index (row-major): x fastest, then y, then z.
[[nodiscard]] constexpr std::size_t index3(int x, int y, int z,
                                           int sizeX, int sizeY) noexcept {
    // Preconditions are the caller's responsibility (0 <= x < sizeX, etc.).
    return static_cast<std::size_t>(x)
         + static_cast<std::size_t>(y) * static_cast<std::size_t>(sizeX)
         + static_cast<std::size_t>(z) * static_cast<std::size_t>(sizeX) * static_cast<std::size_t>(sizeY);
}

// Overload taking an Extent3 (uses x,y,z; z is included for completeness)
[[nodiscard]] constexpr std::size_t index3(int x, int y, int z,
                                           Extent3 dims) noexcept {
    return index3(x, y, z, dims.x, dims.y);
}

// Overload from a 3‑component position type that has .x/.y/.z (e.g., glm::ivec3)
template <class P>
[[nodiscard]] constexpr auto index3(const P& p, Extent3 dims) noexcept
    -> std::enable_if_t<
        std::is_integral_v<decltype(p.x)> &&
        std::is_integral_v<decltype(p.y)> &&
        std::is_integral_v<decltype(p.z)>,
        std::size_t> {
    return index3(static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.z), dims);
}

// ----------------  Optional but very handy: view wrapper  ----------------
template <class T>
class Grid3DView {
public:
    using value_type = T;
    using size_type  = std::size_t;

    Grid3DView() = default;
    Grid3DView(T* ptr, Extent3 dims) : data_(ptr), dims_(dims) {}

    [[nodiscard]] constexpr size_type idx(int x, int y, int z) const noexcept {
        return index3(x, y, z, dims_);
    }

    [[nodiscard]] constexpr T& at(int x, int y, int z) noexcept {
        return data_[idx(x, y, z)];
    }

    [[nodiscard]] constexpr const T& at(int x, int y, int z) const noexcept {
        return data_[idx(x, y, z)];
    }

    [[nodiscard]] constexpr Extent3 dims() const noexcept { return dims_; }

private:
    T*      data_{};   // non-owning
    Extent3 dims_{};   // {sizeX, sizeY, sizeZ}
};

} // namespace worldgen::detail
