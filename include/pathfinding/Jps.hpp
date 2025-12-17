// include/pathfinding/Jps.hpp
#pragma once

#include <vector>

namespace colony::path {

struct Cell {
    int x{};
    int y{};
};

class IGrid {
public:
    virtual ~IGrid() = default;

    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    virtual bool walkable(int x, int y) const = 0;
};

struct JpsOptions {
    // 4-neighbor vs 8-neighbor
    bool allowDiagonal = false;

    // If diagonal, forbid “cutting corners” through blocked orthogonals
    bool dontCrossCorners = true;

    // Adapter output preference:
    // - true  => return dense step-by-step path (grid neighbors)
    // - false => may return sparse “jump points” (depends on core output)
    bool returnDensePath = true;

    // If you later want to expose JPS jump points directly, this flag can be used
    // to keep only jump points even if returnDensePath==true.
    bool preferJumpPoints = false;

    // Optional later enhancement (string pulling / LOS smoothing)
    bool smoothPath = false;
};

// Implemented in pathfinding/JpsAdapter.cpp (bridges to JpsCore)
std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);

// Public API
inline std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    return jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
