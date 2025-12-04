#pragma once
#include <vector>
#include <cstdint>

namespace worldgen {

// Small integer 2D coordinate used across worldgen
struct I2 {
    int x = 0;
    int y = 0;
};

// Basic polyline shared by road/settlement code
struct Polyline {
    std::vector<I2> pts;
};

} // namespace worldgen
