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

/// Octile distance heuristic (integer, admissible).
/// - Appropriate for 8-way movement (diagonals allowed).
/// - Uses an integer approximation with STRAIGHT=10 and DIAG=14, then scales back
///   to the caller's unit cost (1 per orthogonal step) via division. This keeps
///   everything integral and avoids floating point while remaining admissible.
/// - If your search expands only 4-way neighbors, this is still safe (it will
///   simply be more conservative than Manhattan).
inline int heuristic(const Point& a, const Point& b) noexcept {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    const int dmin = (dx < dy) ? dx : dy;
    [[maybe_unused]] const int dmax = (dx < dy) ? dy : dx; // silence potential unused-var warnings in some builds
    constexpr int STRAIGHT = 10; // cost for orthogonal step (scaled)
    constexpr int DIAG     = 14; // ≈ sqrt(2) * STRAIGHT
    // Scale back to unit=1 per orthogonal step to match typical grid costs.
    return (DIAG * dmin + STRAIGHT * (dmax - dmin)) / STRAIGHT; // floor keeps admissible
}

/// Manhattan A* (4-neighborhood). Reconstructs full path including start/end.
/// Returns Result and fills outPath on success.
/// maxExpandedNodes < 0 means “no cap”.
Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::vector<Point>& outPath,
             int maxExpandedNodes = -1);

} // namespace cg::pf
