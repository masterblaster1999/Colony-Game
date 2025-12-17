// pathfinding/JpsAdapter.cpp
#include "pathfinding/Jps.hpp"
#include "JpsCore.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace colony::path {

    // Forward-declare the extended overload (implemented in Jps.cpp in this repo patchline).
    // If your Jps.cpp only has the legacy overload, either:
    //  - add this overload there, or
    //  - change the call below to the legacy signature.
    std::vector<std::pair<int, int>> FindPathJPS(
        const GridView& grid,
        int sx, int sy,
        int gx, int gy,
        bool allowDiagonal,
        bool dontCrossCorners,
        float costStraight,
        float costDiagonal,
        float heuristicWeight,
        bool tieBreakCross);

} // namespace colony::path

namespace colony::path::detail {

namespace {

inline int iabs(int v) noexcept { return (v < 0) ? -v : v; }
inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

// Validates a single step (adjacent cell move) under the movement rules.
static bool step_ok(const IGrid& grid,
                    int x0, int y0,
                    int x1, int y1,
                    bool allowDiagonal,
                    bool dontCrossCorners) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;

    // Must land on walkable cell
    if (!grid.walkable(x1, y1)) return false;

    // No diagonal steps allowed
    if (!allowDiagonal && (dx != 0) && (dy != 0)) return false;

    // Corner-cutting guard for diagonal step
    if (dontCrossCorners && (dx != 0) && (dy != 0)) {
        if (!grid.walkable(x0 + dx, y0)) return false;
        if (!grid.walkable(x0, y0 + dy)) return false;
    }

    return true;
}

// Bresenham line check, respecting movement rules.
// If allowDiagonal==false, only straight row/col LoS is allowed.
static bool line_of_sight(const IGrid& grid, Cell a, Cell b, bool allowDiagonal, bool dontCrossCorners) {
    if (!grid.walkable(a.x, a.y) || !grid.walkable(b.x, b.y)) return false;

    if (!allowDiagonal) {
        if (a.x != b.x && a.y != b.y) return false;
        if (a.x == b.x) {
            const int sy = sgn(b.y - a.y);
            for (int y = a.y; y != b.y; y += sy) {
                if (!step_ok(grid, a.x, y, a.x, y + sy, false, dontCrossCorners)) return false;
            }
            return true;
        } else {
            const int sx = sgn(b.x - a.x);
            for (int x = a.x; x != b.x; x += sx) {
                if (!step_ok(grid, x, a.y, x + sx, a.y, false, dontCrossCorners)) return false;
            }
            return true;
        }
    }

    int x = a.x, y = a.y;
    const int x1 = b.x, y1 = b.y;

    const int dx = iabs(x1 - x);
    const int dy = iabs(y1 - y);
    const int sx = sgn(x1 - x);
    const int sy = sgn(y1 - y);

    int err = dx - dy;

    while (x != x1 || y != y1) {
        const int e2 = err * 2;

        int nx = x;
        int ny = y;

        if (e2 > -dy) { err -= dy; nx += sx; }
        if (e2 <  dx) { err += dx; ny += sy; }

        if (nx == x && ny == y) break; // safety

        if (!step_ok(grid, x, y, nx, ny, true, dontCrossCorners)) return false;

        x = nx;
        y = ny;
    }

    return true;
}

// Simple string-pulling: keep the farthest visible waypoint from the current anchor.
static std::vector<Cell> string_pull(const IGrid& grid,
                                     const std::vector<Cell>& in,
                                     bool allowDiagonal,
                                     bool dontCrossCorners) {
    if (in.size() <= 2) return in;

    std::vector<Cell> out;
    out.reserve(in.size());

    std::size_t anchor = 0;
    out.push_back(in.front());

    while (anchor + 1 < in.size()) {
        std::size_t best = anchor + 1;

        // Greedy extend
        for (std::size_t i = best + 1; i < in.size(); ++i) {
            if (line_of_sight(grid, in[anchor], in[i], allowDiagonal, dontCrossCorners)) {
                best = i;
            } else {
                break;
            }
        }

        out.push_back(in[best]);
        anchor = best;
    }

    return out;
}

// Append all cells along a->b (Bresenham), excluding the first cell (assumes it's already in out).
static bool append_dense_segment(std::vector<Cell>& out,
                                 const IGrid& grid,
                                 Cell a,
                                 Cell b,
                                 bool allowDiagonal,
                                 bool dontCrossCorners) {
    int x = a.x, y = a.y;
    const int x1 = b.x, y1 = b.y;

    if (x == x1 && y == y1) return true;

    // If diagonal movement is disallowed, only allow perfectly straight segments.
    if (!allowDiagonal && (x != x1) && (y != y1)) return false;

    const int dx = iabs(x1 - x);
    const int dy = iabs(y1 - y);
    const int sx = sgn(x1 - x);
    const int sy = sgn(y1 - y);

    int err = dx - dy;

    while (x != x1 || y != y1) {
        const int e2 = err * 2;

        int nx = x;
        int ny = y;

        if (e2 > -dy) { err -= dy; nx += sx; }
        if (e2 <  dx) { err += dx; ny += sy; }

        if (nx == x && ny == y) break;

        if (!step_ok(grid, x, y, nx, ny, allowDiagonal, dontCrossCorners)) return false;

        x = nx;
        y = ny;
        out.push_back(Cell{ x, y });
    }

    return true;
}

} // anonymous namespace

std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
    // Build a GridView wrapper for the existing JPS core implementation.
    colony::path::GridView view{};
    view.width  = grid.width();
    view.height = grid.height();
    view.isBlocked = [&grid](int x, int y) {
        return !grid.walkable(x, y);
    };

    // Sanitize costs
    const float costStraight    = (opt.costStraight > 0.0f)    ? opt.costStraight    : 1.0f;
    const float costDiagonal    = (opt.costDiagonal > 0.0f)    ? opt.costDiagonal    : 1.41421356f;
    const float heuristicWeight = (opt.heuristicWeight > 0.0f) ? opt.heuristicWeight : 1.0f;

    // Run core JPS (jump-point output).
    const auto jp = colony::path::FindPathJPS(
        view,
        start.x, start.y,
        goal.x, goal.y,
        opt.allowDiagonal,
        opt.dontCrossCorners,
        costStraight,
        costDiagonal,
        heuristicWeight,
        opt.tieBreakCross);

    if (jp.empty()) return {};

    // Convert to Cell waypoints.
    std::vector<Cell> waypoints;
    waypoints.reserve(jp.size());
    for (const auto& p : jp) {
        waypoints.push_back(Cell{ p.first, p.second });
    }

    // Optional smoothing (string-pulling) over waypoints.
    if (opt.smoothPath && waypoints.size() > 2) {
        waypoints = string_pull(grid, waypoints, opt.allowDiagonal, opt.dontCrossCorners);
    }

    // Densify between waypoints so callers get an actual step-by-step grid path.
    std::vector<Cell> dense;
    dense.reserve(std::max<std::size_t>(waypoints.size(), 2u));
    dense.push_back(waypoints.front());

    for (std::size_t i = 1; i < waypoints.size(); ++i) {
        if (!append_dense_segment(dense, grid, waypoints[i - 1], waypoints[i],
                                  opt.allowDiagonal, opt.dontCrossCorners)) {
            return {};
        }
    }

    return dense;
}

} // namespace colony::path::detail
