// src/pathfinding/jps.h
#pragma once
// Public JPS API and minimal types used by the pathfinder.

#include <vector>
#include <cstdint>

namespace colony::path {

// A grid cell in integer grid coordinates (row/column or x/y).
struct Cell {
    int x{};
    int y{};
    constexpr bool operator==(const Cell&) const = default;
};

// Options for JPS. Extend as your implementation needs.
struct JpsOptions {
    bool allow_diagonals       = true;   // 8-way movement
    bool allow_corner_cutting  = false;  // forbid diagonal through tight corners
    bool use_octile_heuristic  = true;   // typical heuristic for 8-way grids
};

// Abstract grid interface used by the pathfinder.
struct IGrid {
    virtual ~IGrid() = default;
    virtual bool is_walkable(int x, int y) const = 0;
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
};

// Find an optimal path from start to goal on 'grid' using JPS.
std::vector<Cell>
jps_find_path(IGrid const& grid, Cell start, Cell goal, JpsOptions const& opts);

} // namespace colony::path
