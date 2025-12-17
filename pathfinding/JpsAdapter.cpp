// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp public API to legacy JPS core (JpsCore.hpp / FindPathJPS).

#include "pathfinding/Jps.hpp"
#include "pathfinding/JpsCore.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace colony::path {
namespace detail {
namespace jps_adapter_detail {

static inline int sign_i(int v) {
    return (v > 0) - (v < 0);
}

// Conservative "can step" check for a grid move from (x0,y0) to (x1,y1).
static bool step_ok(const GridView& gv,
                    int x0, int y0,
                    int x1, int y1,
                    bool allowDiag,
                    bool dontCrossCorners) {
    if (!gv.walkable(x1, y1)) return false;

    const int dx = x1 - x0;
    const int dy = y1 - y0;

    // Orthogonal step
    if (dx == 0 || dy == 0) return true;

    // Diagonal step
    if (!allowDiag) return false;
    if (!dontCrossCorners) return true;

    // Corner guard: for diagonal movement, require both adjacent orthogonals to be free.
    return gv.walkable(x0 + dx, y0) && gv.walkable(x0, y0 + dy);
}

} // namespace jps_adapter_detail
} // namespace detail

std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    // Validate endpoints (tests expect empty path if start OR goal blocked)
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) {
        return {};
    }

    // Tests expect size==1 if start==goal
    if (start.x == goal.x && start.y == goal.y) {
        return { start };
    }

    // Wrap the public grid into the core GridView
    GridView gv{
        grid.width(),
        grid.height(),
        [&](int x, int y) { return grid.walkable(x, y); }
    };

    FindPathJPS jps(gv);
    jps.allowDiag        = opt.allowDiagonal;
    jps.dontCrossCorners = opt.dontCrossCorners;

    const auto jpPath = jps.find({ start.x, start.y }, { goal.x, goal.y });
    if (jpPath.empty()) {
        return {};
    }

    // Convert Point -> Cell
    std::vector<Cell> out;
    out.reserve(jpPath.size());
    for (const auto& p : jpPath) {
        out.push_back(Cell{ p.x, p.y });
    }

    // Optional: expand “jump points” into dense step-by-step path
    // (Your tests only assert endpoints, but dense paths are generally friendlier for movers.)
    if (opt.returnDensePath && !opt.preferJumpPoints && out.size() >= 2) {
        std::vector<Cell> dense;
        dense.reserve(out.size() * 2);
        dense.push_back(out.front());

        for (std::size_t i = 1; i < out.size(); ++i) {
            const Cell a = out[i - 1];
            const Cell b = out[i];

            const int sx = detail::jps_adapter_detail::sign_i(b.x - a.x);
            const int sy = detail::jps_adapter_detail::sign_i(b.y - a.y);

            int x = a.x;
            int y = a.y;

            while (x != b.x || y != b.y) {
                const int nx = x + sx;
                const int ny = y + sy;

                if (!detail::jps_adapter_detail::step_ok(gv, x, y, nx, ny,
                                                        opt.allowDiagonal,
                                                        opt.dontCrossCorners)) {
                    // If we can’t safely densify, fall back to jump points.
                    dense.clear();
                    break;
                }

                x = nx;
                y = ny;
                dense.push_back(Cell{ x, y });
            }

            if (dense.empty()) {
                break;
            }
        }

        if (!dense.empty()) {
            out = std::move(dense);
        }
    }

    // Future: opt.smoothPath could do LOS-based string pulling here.
    return out;
}

} // namespace colony::path
