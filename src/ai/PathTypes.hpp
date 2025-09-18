// src/ai/PathTypes.hpp
#pragma once
#include <cstddef>
#include <vector>

namespace ai {

// Integer grid coordinate used by pathfinding.
struct Point {
    int x{0};
    int y{0};
    friend constexpr bool operator==(const Point& a, const Point& b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(const Point& a, const Point& b) noexcept {
        return !(a == b);
    }
};

// Result flag for pathfinding queries.
enum class PFResult { Found, NotFound };

// Convenience alias used by higher-level code.
using Path = std::vector<Point>;

} // namespace ai
