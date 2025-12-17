#pragma once
#include <vector>
#include <cstdint>
#include <limits>
#include <functional>

namespace colony::path {

// Simple integer point
struct Cell { int x{}, y{}; };

// Map abstraction (adapt this with your tilemap)
struct IGrid {
    virtual ~IGrid() = default;
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    // Return true if the cell is inside the map AND traversable.
    virtual bool walkable(int x, int y) const = 0;
};

// Options that match common colony sim needs
struct JpsOptions {
    bool allowDiagonal      = true;   // 8-connected movement
    bool dontCrossCorners   = true;   // forbid corner cutting on diagonals
    float costStraight      = 1.0f;   // cost of N/E/S/W
    float costDiagonal      = 1.41421356237f; // ~= sqrt(2)
    float heuristicWeight   = 1.0f;   // 1.0 keeps A* optimal
    bool  tieBreakCross     = true;   // favor straighter paths slightly
    bool  smoothPath        = false;  // optional string-pulling smoothing
};

namespace detail {

// IMPORTANT:
//   This header now provides a small *wrapper* around the real implementation.
//   You must provide this function in a .cpp (or rename your existing
//   out-of-line `jps_find_path` to this name/signature).
//
// Example rename (in your existing Jps.cpp):
//   std::vector<Cell> colony::path::jps_find_path(...)  --> 
//   std::vector<Cell> colony::path::detail::jps_find_path_impl(...)
std::vector<Cell> jps_find_path_impl(
    const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);

} // namespace detail

// Return empty vector when no path exists.
// The output path includes both start and goal.
inline std::vector<Cell> jps_find_path(
    const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt = {})
{
    // PATCH: StartEqualsGoal should return {start} (if the cell is walkable).
    if (start.x == goal.x && start.y == goal.y) {
        if (!grid.walkable(start.x, start.y))
            return {};
        return { start };
    }

    // Defensive validation (also keeps behavior sensible for blocked start/goal).
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y))
        return {};

    return detail::jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
