// src/ai/Pathfinding.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include <limits>
#include <cstdlib>  // std::abs

namespace cg::pf {

struct Point { int x{0}, y{0}; };

struct GridView {
    int w{0}, h{0};
    // Return whether a tile can be traversed.
    std::function<bool(int,int)> walkable;
    // Non-negative step cost for entering (x,y). Use >=1 for “normal”.
    std::function<int(int,int)>   cost;

    bool  inBounds(int x, int y) const noexcept { return x>=0 && y>=0 && x<w && y<h; }
    int   index(int x, int y)     const noexcept { return y*w + x; }
    Point fromIndex(int i)        const noexcept { return { i % w, i / w }; }
};

enum class Result { Found, NoPath, Aborted };

/// Manhattan A* (4-neighborhood). Reconstructs full path including start/end.
/// Returns Result and fills outPath on success.
/// maxExpandedNodes < 0 means “no cap”.
Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::vector<Point>& outPath,
             int maxExpandedNodes = -1);

} // namespace cg::pf
