// include/pathfinding/Jps.hpp
#pragma once

#include <vector>

namespace colony::path {

// Simple integer grid cell
struct Cell {
    int x{};
    int y{};
};

// Minimal grid interface used by the public JPS API
class IGrid {
public:
    virtual ~IGrid() = default;
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    // Should return false for out-of-bounds.
    virtual bool walkable(int x, int y) const = 0;
};

// Options exposed by the public JPS API
struct JpsOptions {
    // Movement rules
    bool allowDiagonal    = true;
    bool dontCrossCorners = true;

    // Costs (used by the underlying search if supported)
    float costStraight    = 1.0f;
    float costDiagonal    = 1.41421356f; // ~sqrt(2)
    float heuristicWeight = 1.0f;        // 1.0 = admissible (if heuristic matches costs)

    // Optional behaviors
    bool tieBreakCross = false;
    bool smoothPath    = false;
};

namespace detail {
    // Implemented in pathfinding/JpsAdapter.cpp (in colony_path target)
    std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);
}

// Public entry point
inline std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt = {}) {
    // Start==Goal special case
    if (start.x == goal.x && start.y == goal.y) {
        if (!grid.walkable(start.x, start.y)) return {};
        return { start };
    }

    // Blocked start/goal guard
    if (!grid.walkable(start.x, start.y)) return {};
    if (!grid.walkable(goal.x, goal.y))   return {};

    return detail::jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
