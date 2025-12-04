#pragma once
#include <algorithm>
#include <cstddef>

namespace worldgen {
inline size_t index3(int x, int y, int W) {
    return static_cast<size_t>(y) * static_cast<size_t>(W)
         + static_cast<size_t>(x);
}
inline bool inb(int x, int y, int W, int H) {
    return static_cast<unsigned>(x) < static_cast<unsigned>(W)
        && static_cast<unsigned>(y) < static_cast<unsigned>(H);
}
inline float clamp01(float v) {
    return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace worldgen
