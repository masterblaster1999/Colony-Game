#pragma once
// JPS_OctileHeuristic.hpp — admissible & consistent heuristics for 4- and 8-connected grids.

#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace jps {

struct HCoord { int x, y; };

inline float manhattan(const HCoord& a, const HCoord& b) {
    const float dx = static_cast<float>(std::abs(a.x - b.x));
    const float dy = static_cast<float>(std::abs(a.y - b.y));
    return dx + dy;
}

inline float octile(const HCoord& a, const HCoord& b) {
    const float dx = static_cast<float>(std::abs(a.x - b.x));
    const float dy = static_cast<float>(std::abs(a.y - b.y));
    const float D  = 1.0f;
    const float D2 = 1.41421356237f;
    return D * (dx + dy) + (D2 - 2.0f * D) * (std::min)(dx, dy);
}

} // namespace jps
