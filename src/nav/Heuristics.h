#pragma once
#include <cmath>
#include "GridTypes.h"

namespace colony::nav {

// Octile distance: standard for 8-connected grids with unit cardinal and sqrt(2) diagonal costs.
// Well-suited to JPS/HPA* on uniform grids.
inline float Octile(const Coord& a, const Coord& b) {
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    int mn = dx < dy ? dx : dy;
    int mx = dx ^ dy ? (dx + dy - mn) : (dx + dy - mn); // avoid branch mispredicts; equivalent to max
    return kCostDiagonal * static_cast<float>(mn)
         + kCostStraight * static_cast<float>(mx - mn);
}

inline float Manhattan(const Coord& a, const Coord& b) {
    return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

} // namespace colony::nav
