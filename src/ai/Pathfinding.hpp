#pragma once

#include <vector>
#include <queue>
#include <functional>
#include <cstdint>
#include <limits>
#include <algorithm>

namespace cg::pf {

struct Point {
    int x{0}, y{0};
};

/// Lightweight grid facade the pathfinder queries.
/// Provide `walkable(x,y)` and `cost(x,y)` when you fill this in.
struct GridView {
    int w{0}, h{0};

    // Return whether a tile can be traversed.
    std::function<bool(int,int)> walkable;
    // Non-negative step cost for entering (x,y). Use >= 1 for “normal”.
    std::function<int(int,int)>  cost;

    [[nodiscard]] bool inBounds(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < w && y < h;
    }
    [[nodiscard]] constexpr int index(int x, int y) const noexcept {
        return y * w + x;
    }
    [[nodiscard]] Point fromIndex(int idx) const noexcept {
        return { idx % w, idx / w };
    }
};

enum class Result {
    Found,
    NoPath,
    Aborted  // if maxExpandedNodes budget is hit
};

/// A* on a 4-neighbor grid. Writes the path to `outPath` (start..goal).
/// If `maxExpandedNodes >= 0`, the search aborts after exceeding that budget.
[[nodiscard]] Result aStar(const GridView& g,
                           const Point start,
                           const Point goal,
                           std::vector<Point>& outPath,
                           const int maxExpandedNodes = -1);

} // namespace cg::pf
