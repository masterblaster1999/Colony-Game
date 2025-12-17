// include/pathfinding/Jps.hpp
#pragma once

// Public API header.
// IMPORTANT (Windows/MSVC include-shadowing fix):
// Include this file via the public include root using angle brackets:
//
//     #include <pathfinding/Jps.hpp>
//
// Using <...> avoids accidentally picking up a different "pathfinding/Jps.hpp" from a
// nearer directory when include paths overlap (a common Windows CI build-breaker).
//
// This macro is intentionally defined so implementation files can sanity-check they
// included the correct header (e.g. in JpsAdapter.cpp):
//     static_assert(COLONY_PATHFINDING_JPS_HPP_PUBLIC == 1);

#ifndef COLONY_PATHFINDING_JPS_HPP_PUBLIC
#define COLONY_PATHFINDING_JPS_HPP_PUBLIC 1
#endif

#include <vector>

namespace colony::path {

struct Cell {
    int x{};
    int y{};

    constexpr Cell() = default;
    constexpr Cell(int x_, int y_) : x(x_), y(y_) {}

    friend constexpr bool operator==(const Cell& a, const Cell& b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(const Cell& a, const Cell& b) noexcept {
        return !(a == b);
    }
};

class IGrid {
public:
    virtual ~IGrid() = default;

    [[nodiscard]] virtual int  width() const = 0;
    [[nodiscard]] virtual int  height() const = 0;
    [[nodiscard]] virtual bool walkable(int x, int y) const = 0;
};

struct JpsOptions {
    // 4-neighbor vs 8-neighbor
    bool allowDiagonal{false};

    // If diagonal, forbid “cutting corners” through blocked orthogonals
    bool dontCrossCorners{true};

    // Adapter output preference:
    // - true  => return dense step-by-step path (grid neighbors)
    // - false => may return sparse “jump points” (depends on core output)
    bool returnDensePath{true};

    // If you later want to expose JPS jump points directly, this flag can be used
    // to keep only jump points even if returnDensePath==true.
    bool preferJumpPoints{false};

    // Optional later enhancement (string pulling / LOS smoothing)
    bool smoothPath{false};
};

// Implemented in pathfinding/JpsAdapter.cpp (bridges to JpsCore).
// NOTE: This is "impl" on purpose; prefer calling jps_find_path() from gameplay code.
[[nodiscard]] std::vector<Cell>
jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt);

// Public API
[[nodiscard]] inline std::vector<Cell>
jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    return jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
