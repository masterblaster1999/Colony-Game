#pragma once
#include <vector>
#include <cstddef>

namespace worldgen {
struct I2 {
    int x{0}, y{0};
    constexpr I2() = default;
    constexpr I2(int x_, int y_) noexcept : x(x_), y(y_) {}
};
struct Polyline { std::vector<I2> pts; };
} // namespace worldgen
