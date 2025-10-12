#pragma once
#include "GridTypes.hpp"
#include <cmath>

namespace colony::pf {

// Octile distance: admissible/consistent for 8-dir grids with costs 1, √2
inline float octile(int dx, int dy, float D=1.0f, float D2=1.41421356237f) {
    dx = std::abs(dx); dy = std::abs(dy);
    const int m = (dx < dy ? dx : dy);
    const int M = (dx < dy ? dy : dx);
    return D * float(M - m) + D2 * float(m);
}

inline float octile(NodeId a, NodeId b, int w) {
    const auto A = from_id(a, w);
    const auto B = from_id(b, w);
    return octile(B.x - A.x, B.y - A.y);
}

} // namespace colony::pf
