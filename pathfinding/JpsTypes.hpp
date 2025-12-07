// pathfinding/JpsTypes.hpp
#pragma once
#include <vector>
#include <cstdint>

namespace colony::path {

struct Cell { int x = 0; int y = 0; };

struct IGrid {
    virtual int  width()  const noexcept = 0;
    virtual int  height() const noexcept = 0;
    virtual bool walkable(int x, int y) const noexcept = 0;
    virtual ~IGrid() = default;
};

struct JpsOptions {
    bool  allowDiagonal    = true;
    bool  dontCrossCorners = true;   // forbid diagonal corner cutting
    bool  smoothPath       = true;   // LOS smoothing
    bool  tieBreakCross    = true;   // tiny cross-product tie-break
    float costStraight     = 1.0f;   // grid step
    float costDiagonal     = 1.41421356237f; // octile diagonal
    float heuristicWeight  = 1.0f;   // A* weight (1.0 => admissible)
};

// Public API
std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);

} // namespace colony::path
