#pragma once

#include <vector>

namespace colony::path {

struct Cell {
    int x = 0;
    int y = 0;
};

struct JpsOptions {
    // Movement rules
    bool allowDiagonal    = true;
    bool dontCrossCorners = true; // if true: diagonal step requires both orthogonal neighbors

    // (Not yet wired into the JPS core in this patch, but kept for API stability)
    float costStraight    = 1.0f;
    float costDiagonal    = 1.41421356237f;
    float heuristicWeight = 1.0f;
    bool  tieBreakCross   = false;

    // Post-process returned path
    bool smoothPath       = false; // if true: string-pull (line-of-sight) smoothing
};

struct IGrid {
    virtual ~IGrid() = default;
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    virtual bool walkable(int x, int y) const = 0;
};

namespace detail {
// Implemented in pathfinding/JpsAdapter.cpp
std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);
} // namespace detail

inline std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt = {}) {
    return detail::jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
