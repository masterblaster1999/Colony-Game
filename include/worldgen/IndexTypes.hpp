#pragma once
#include <cstdint>

namespace worldgen {

// Simple integer 2D coordinate used by worldgen & roads
struct I2 {
    int x{};
    int y{};
    constexpr I2() = default;
    constexpr I2(int x_, int y_) : x(x_), y(y_) {}
    friend constexpr bool operator==(const I2& a, const I2& b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(const I2& a, const I2& b) noexcept {
        return !(a == b);
    }
};

} // namespace worldgen
