// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp public API to the JPS core (pathfinding/JpsCore.hpp).

// IMPORTANT (Windows/MSVC include-shadowing fix):
// Include the public header via its *actual* repo path to avoid picking up an unintended
// "pathfinding/Jps.hpp" from some other include directory (common on Windows CI).
#include "include/pathfinding/Jps.hpp"

// Sanity-check we got the intended public header.
#ifndef COLONY_PATHFINDING_JPS_HPP_PUBLIC
    #error "Wrong Jps.hpp included. This .cpp must include the repo's public header at include/pathfinding/Jps.hpp."
#endif
static_assert(COLONY_PATHFINDING_JPS_HPP_PUBLIC == 1, "Wrong Jps.hpp included (public header macro mismatch).");

#include "pathfinding/JpsCore.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace colony::path {
namespace detail {

namespace {

// Returns -1, 0, or 1.
constexpr int sign_i(int v) noexcept { return (v > 0) - (v < 0); }

// Validate a single step (x0,y0) -> (x1,y1) against movement rules.
bool step_ok(const GridView& gv,
             int x0, int y0, int x1, int y1,
             bool allowDiagonal,
             bool dontCrossCorners) noexcept
{
    if (!gv.passable(x1, y1)) return false;

    const int dx = x1 - x0;
    const int dy = y1 - y0;

    // Disallow teleport steps (Bresenham should never produce these).
    if (std::abs(dx) > 1 || std::abs(dy) > 1) return false;

    if (dx != 0 && dy != 0) {
        // Diagonal move.
        if (!allowDiagonal) return false;

        if (dontCrossCorners) {
            // For diagonal movement, require both adjacent orthogonals.
            if (!gv.passable(x0 + dx, y0) || !gv.passable(x0, y0 + dy)) return false;
        }
    }

    return true;
}

// Bresenham line walk; returns false if any visited cell/step violates movement rules.
bool line_ok(const GridView& gv,
             Cell a,
             Cell b,
             bool allowDiagonal,
             bool dontCrossCorners) noexcept
{
    int x0 = a.x, y0 = a.y;
    const int x1 = b.x, y1 = b.y;

    int dx = std::abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;

    int dy = -std::abs(y1 - y0);
    const int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (true) {
        if (!gv.passable(x0, y0)) return false;
        if (x0 == x1 && y0 == y1) break;

        const int e2 = 2 * err;
        int nx = x0;
        int ny = y0;

        if (e2 >= dy) { err += dy; nx += sx; }
        if (e2 <= dx) { err += dx; ny += sy; }

        if (!step_ok(gv, x0, y0, nx, ny, allowDiagonal, dontCrossCorners)) return false;

        x0 = nx;
        y0 = ny;
    }

    return true;
}

// If consecutive points aren't adjacent, expand segments into step-by-step cells.
// If expansion fails validation, we return the input unchanged (safe fallback).
std::vector<Cell> densify_if_needed(const GridView& gv,
                                    const std::vector<Cell>& in,
                                    const JpsOptions& opt)
{
    if (in.size() <= 1) return in;

    bool has_gaps = false;
    for (std::size_t i = 1; i < in.size(); ++i) {
        const int dx = std::abs(in[i].x - in[i - 1].x);
        const int dy = std::abs(in[i].y - in[i - 1].y);
        if (dx > 1 || dy > 1) { has_gaps = true; break; }
    }
    if (!has_gaps) return in;

    std::vector<Cell> dense;
    dense.reserve(in.size() * 2);
    dense.push_back(in.front());

    for (std::size_t i = 1; i < in.size(); ++i) {
        const Cell target = in[i];

        int x0 = dense.back().x;
        int y0 = dense.back().y;
        const int x1 = target.x;
        const int y1 = target.y;

        int dx = std::abs(x1 - x0);
        const int sx = (x0 < x1) ? 1 : -1;

        int dy = -std::abs(y1 - y0);
        const int sy = (y0 < y1) ? 1 : -1;

        int err = dx + dy;

        while (!(x0 == x1 && y0 == y1)) {
            const int e2 = 2 * err;
            int nx = x0;
            int ny = y0;

            if (e2 >= dy) { err += dy; nx += sx; }
            if (e2 <= dx) { err += dx; ny += sy; }

            if (!step_ok(gv, x0, y0, nx, ny, opt.allowDiagonal, opt.dontCrossCorners)) {
                // Don't “invent” a path that violates rules; return original.
                return in;
            }

            x0 = nx;
            y0 = ny;
            dense.push_back(Cell{ x0, y0 });
        }
    }

    return dense;
}

// Greedy string-pull: keep farthest visible point from current anchor.
std::vector<Cell> smooth_waypoints(const GridView& gv,
                                   const std::vector<Cell>& dense,
                                   const JpsOptions& opt)
{
    if (dense.size() <= 2) return dense;

    std::vector<Cell> out;
    out.reserve(dense.size());
    out.push_back(dense.front());

    std::size_t anchor = 0;
    while (anchor + 1 < dense.size()) {
        std::size_t best = anchor + 1;

        // Try to push best forward while LOS holds.
        for (std::size_t j = best + 1; j < dense.size(); ++j) {
            if (line_ok(gv, dense[anchor], dense[j], opt.allowDiagonal, opt.dontCrossCorners)) {
                best = j;
            } else {
                // Dense path is monotonic-ish; break early for speed.
                break;
            }
        }

        out.push_back(dense[best]);
        anchor = best;
    }

    // Ensure goal is present.
    if (out.back().x != dense.back().x || out.back().y != dense.back().y) {
        out.push_back(dense.back());
    }

    return out;
}

// ---- Core-path call selection (no C++20 'requires' needed) ----
// Prefer extended overload if BOTH the overload exists AND JpsOptions provides the needed fields.
// Otherwise fall back to the simpler overloads.
//
// This fixes the build break where JpsAdapter.cpp referenced opt.costStraight / etc even when
// JpsOptions didn't define them (and avoids non-dependent requires-expressions).
template <class Opt>
auto find_core_path_impl(const GridView& gv, Cell start, Cell goal, const Opt& opt, int)
    -> decltype(FindPathJPS(gv,
                            start.x, start.y,
                            goal.x, goal.y,
                            opt.allowDiagonal,
                            opt.dontCrossCorners,
                            opt.costStraight,
                            opt.costDiagonal,
                            opt.heuristicWeight,
                            opt.tieBreakCross))
{
    return FindPathJPS(gv,
                       start.x, start.y,
                       goal.x, goal.y,
                       opt.allowDiagonal,
                       opt.dontCrossCorners,
                       opt.costStraight,
                       opt.costDiagonal,
                       opt.heuristicWeight,
                       opt.tieBreakCross);
}

template <class Opt>
auto find_core_path_impl(const GridView& gv, Cell start, Cell goal, const Opt& opt, long)
    -> decltype(FindPathJPS(gv,
                            start.x, start.y,
                            goal.x, goal.y,
                            opt.allowDiagonal,
                            opt.dontCrossCorners))
{
    return FindPathJPS(gv,
                       start.x, start.y,
                       goal.x, goal.y,
                       opt.allowDiagonal,
                       opt.dontCrossCorners);
}

template <class Opt>
auto find_core_path_impl(const GridView& gv, Cell start, Cell goal, const Opt& opt, ...)
    -> decltype(FindPathJPS(gv,
                            start.x, start.y,
                            goal.x, goal.y,
                            opt.allowDiagonal))
{
    return FindPathJPS(gv,
                       start.x, start.y,
                       goal.x, goal.y,
                       opt.allowDiagonal);
}

template <class Opt>
auto find_core_path(const GridView& gv, Cell start, Cell goal, const Opt& opt)
{
    // Prefer best-match overload first (int), then fallback (long), then final fallback (...).
    return find_core_path_impl(gv, start, goal, opt, 0);
}

} // unnamed namespace

std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
{
    // Defensive: public wrapper already checks these.
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) return {};

    GridView gv{
        grid.width(),
        grid.height(),
        [&grid](int x, int y) { return !grid.walkable(x, y); } // true => blocked
    };

    const auto corePath = find_core_path(gv, start, goal, opt);
    if (corePath.empty()) return {};

    std::vector<Cell> out;
    out.reserve(corePath.size());
    for (const auto& p : corePath) {
        out.push_back(Cell{ p.first, p.second });
    }

    // Make output safe for “move cell-by-cell” consumers.
    out = densify_if_needed(gv, out, opt);

    // Optional: string-pull, then densify again so the mover still gets step-by-step cells.
    if (opt.smoothPath) {
        const auto waypoints = smooth_waypoints(gv, out, opt);
        out = densify_if_needed(gv, waypoints, opt);
    }

    return out;
}

} // namespace detail

// Compatibility wrapper:
// - Public header in include/pathfinding/Jps.hpp declares colony::path::jps_find_path_impl
// - Older/internal callers may use colony::path::detail::jps_find_path_impl
//
// Keeping this wrapper prevents link errors if the declaration lives in colony::path.
std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
{
    return detail::jps_find_path_impl(grid, start, goal, opt);
}

} // namespace colony::path
