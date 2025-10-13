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

// Return empty vector when no path exists.
// The output path includes both start and goal.
std::vector<Cell> jps_find_path(
    const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt = {});

} // namespace colony::path
