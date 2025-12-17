// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp (IGrid-based API) to pathfinding/JpsCore.hpp (GridView-based JPS).

#include "pathfinding/Jps.hpp"
#include "JpsCore.hpp"

#include <algorithm>
#include <cstdlib>   // std::abs
#include <utility>
#include <vector>

namespace {

inline int sign_i(int v) noexcept {
    return (v > 0) ? 1 : (v < 0) ? -1 : 0;
}

// Validate a *single step* from (x0,y0) -> (x1,y1) under the movement rules.
static bool step_ok(const colony::path::IGrid& grid,
                    int x0, int y0, int x1, int y1,
                    bool allowDiagonal,
                    bool dontCrossCorners)
{
    const int dx  = x1 - x0;
    const int dy  = y1 - y0;
    const int adx = std::abs(dx);
    const int ady = std::abs(dy);

    // Must be a 4-neighbor or 8-neighbor step (or no-op).
    if (adx > 1 || ady > 1) return false;
    if (adx == 0 && ady == 0) return true;

    if (!grid.walkable(x1, y1)) return false;

    const bool diagonal = (adx == 1 && ady == 1);
    if (diagonal) {
        if (!allowDiagonal) return false;

        if (dontCrossCorners) {
            // For diagonal move, both adjacent orthogonal cells must be walkable.
            if (!grid.walkable(x0 + dx, y0) || !grid.walkable(x0, y0 + dy)) {
                return false;
            }
        }
    }

    return true;
}

// Expand a jump-point path (waypoints) into a dense step-by-step path.
static std::vector<colony::path::Cell>
expand_jump_points(const colony::path::IGrid& grid,
                   const std::vector<std::pair<int, int>>& jp,
                   bool allowDiagonal,
                   bool dontCrossCorners)
{
    std::vector<colony::path::Cell> out;
    if (jp.empty()) return out;

    out.reserve(jp.size()); // minimum; may grow during expansion

    int x = jp.front().first;
    int y = jp.front().second;
    out.push_back({x, y});

    for (size_t i = 1; i < jp.size(); ++i) {
        const int tx = jp[i].first;
        const int ty = jp[i].second;

        const int sx = sign_i(tx - x);
        const int sy = sign_i(ty - y);

        // Defensive: if a segment is degenerate, skip it.
        if (sx == 0 && sy == 0) continue;

        // Walk along the segment one step at a time.
        while (x != tx || y != ty) {
            const int nx = x + sx;
            const int ny = y + sy;

            if (!step_ok(grid, x, y, nx, ny, allowDiagonal, dontCrossCorners)) {
                // Should not happen if the core JPS is correct, but don't return nonsense.
                return {};
            }

            x = nx;
            y = ny;
            out.push_back({x, y});
        }
    }

    return out;
}

// Bresenham-style line-of-sight: can we go from a -> b without hitting blocked cells,
// respecting corner-cutting rules for diagonal sub-steps.
static bool line_of_sight(const colony::path::IGrid& grid,
                          colony::path::Cell a,
                          colony::path::Cell b,
                          bool allowDiagonal,
                          bool dontCrossCorners)
{
    int x0 = a.x, y0 = a.y;
    const int x1 = b.x, y1 = b.y;

    if (!grid.walkable(x0, y0) || !grid.walkable(x1, y1)) return false;

    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (x0 != x1 || y0 != y1) {
        const int e2 = err * 2;

        int nx = x0;
        int ny = y0;

        if (e2 > -dy) { err -= dy; nx += sx; }
        if (e2 <  dx) { err += dx; ny += sy; }

        if (!step_ok(grid, x0, y0, nx, ny, allowDiagonal, dontCrossCorners)) {
            return false;
        }

        x0 = nx;
        y0 = ny;
    }

    return true;
}

// Simple string-pull: choose the farthest visible point from the current anchor.
static std::vector<colony::path::Cell>
smooth_string_pull(const colony::path::IGrid& grid,
                   const std::vector<colony::path::Cell>& in,
                   bool allowDiagonal,
                   bool dontCrossCorners)
{
    if (in.size() <= 2) return in;

    std::vector<colony::path::Cell> out;
    out.reserve(in.size());

    size_t anchor = 0;
    out.push_back(in.front());

    while (anchor + 1 < in.size()) {
        size_t best = anchor + 1;

        for (size_t j = anchor + 1; j < in.size(); ++j) {
            if (line_of_sight(grid, in[anchor], in[j], allowDiagonal, dontCrossCorners)) {
                best = j;
            }
        }

        out.push_back(in[best]);
        anchor = best;
    }

    // Ensure we end exactly at goal.
    if (out.empty() || out.back().x != in.back().x || out.back().y != in.back().y) {
        out.push_back(in.back());
    }

    return out;
}

} // anonymous namespace

namespace colony::path::detail {

std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    // Start/goal validation (your tests expect empty on blocked start/goal).
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) {
        return {};
    }

    if (start.x == goal.x && start.y == goal.y) {
        return { start };
    }

    // Adapt IGrid -> GridView expected by the JPS core.
    colony::path::GridView view{};
    view.width  = grid.width();
    view.height = grid.height();
    view.isBlocked = [&grid](int x, int y) -> bool {
        return !grid.walkable(x, y);
    };

    // Core JPS returns jump points / waypoints as pairs.
    const auto jumpPoints = colony::path::FindPathJPS(
        view,
        start.x, start.y,
        goal.x,  goal.y,
        opt.allowDiagonal,
        opt.dontCrossCorners
    );

    if (jumpPoints.empty()) {
        return {};
    }

    // Expand to a dense path (step-by-step).
    auto dense = expand_jump_points(grid, jumpPoints, opt.allowDiagonal, opt.dontCrossCorners);
    if (dense.empty()) {
        return {};
    }

    // Optional smoothing.
    if (opt.smoothPath) {
        dense = smooth_string_pull(grid, dense, opt.allowDiagonal, opt.dontCrossCorners);
    }

    return dense;
}

} // namespace colony::path::detail
