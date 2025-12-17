// pathfinding/JpsAdapter.cpp
//
// Bridges the public API in include/pathfinding/Jps.hpp (IGrid/Cell/JpsOptions)
// to the internal GridView-based JPS core (JpsCore.hpp + Jps.cpp).

#include "pathfinding/Jps.hpp"
#include "JpsCore.hpp"

#include <cstddef>
#include <cstdlib>   // std::abs
#include <utility>
#include <vector>

namespace colony::path::detail {

namespace {

static inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

// Basic Bresenham-style grid LOS.
// - Uses grid.walkable() (so OOB should return false).
// - If a diagonal step occurs and dontCrossCorners is true, requires both side-adjacent cells open.
static bool line_of_sight(
    const IGrid& grid,
    Cell a,
    Cell b,
    bool allowDiagonal,
    bool dontCrossCorners)
{
    if (!grid.walkable(a.x, a.y) || !grid.walkable(b.x, b.y)) return false;

    int x = a.x;
    int y = a.y;

    const int x1 = b.x;
    const int y1 = b.y;

    const int dx = std::abs(x1 - x);
    const int dy = std::abs(y1 - y);

    const int sx = sgn(x1 - x);
    const int sy = sgn(y1 - y);

    int err = dx - dy;

    while (!(x == x1 && y == y1)) {
        const int e2 = err * 2;

        int nx = x;
        int ny = y;

        if (e2 > -dy) { err -= dy; nx += sx; }
        if (e2 <  dx) { err += dx; ny += sy; }

        // Diagonal step?
        if (nx != x && ny != y) {
            if (!allowDiagonal) return false;

            if (dontCrossCorners) {
                if (!grid.walkable(nx, y) || !grid.walkable(x, ny)) return false;
            }
        }

        x = nx;
        y = ny;

        if (!grid.walkable(x, y)) return false;
    }

    return true;
}

static std::vector<Cell> smooth_string_pull(
    const IGrid& grid,
    const std::vector<Cell>& in,
    const JpsOptions& opt)
{
    if (in.size() <= 2) return in;

    std::vector<Cell> out;
    out.reserve(in.size());

    std::size_t anchor = 0;
    out.push_back(in.front());

    for (std::size_t i = 2; i < in.size(); ++i) {
        if (!line_of_sight(grid, in[anchor], in[i], opt.allowDiagonal, opt.dontCrossCorners)) {
            out.push_back(in[i - 1]);
            anchor = i - 1;
        }
    }

    out.push_back(in.back());
    return out;
}

} // namespace

std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    // Match test expectations / keep behavior obvious:
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) return {};
    if (start.x == goal.x && start.y == goal.y) return { start };

    GridView view;
    view.width  = grid.width();
    view.height = grid.height();
    view.isBlocked = [&grid](int x, int y) -> bool {
        return !grid.walkable(x, y);
    };

    // Options now drive the algorithm:
    std::vector<std::pair<int, int>> raw = FindPathJPS(
        view,
        start.x, start.y,
        goal.x, goal.y,
        opt.allowDiagonal,
        opt.dontCrossCorners,
        opt.costStraight,
        opt.costDiagonal,
        opt.heuristicWeight,
        opt.tieBreakCross);

    std::vector<Cell> out;
    out.reserve(raw.size());
    for (const auto& p : raw) {
        out.push_back(Cell{ p.first, p.second });
    }

    if (opt.smoothPath) {
        out = smooth_string_pull(grid, out, opt);
    }

    return out;
}

} // namespace colony::path::detail
