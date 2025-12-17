// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp (IGrid-based API) to pathfinding/JpsCore.hpp (GridView-based JPS).
//
// This implementation also wires JpsOptions.costStraight/costDiagonal/heuristicWeight/tieBreakCross
// into the tuned JPS core overload, and optionally applies string-pulling smoothing.
//
// MSVC/unity-build friendly:
// - No global helper names in the root namespace.
// - No reliance on PCH for <vector>/<algorithm>/<cstdint>.

#include "pathfinding/Jps.hpp" // IGrid, Cell, JpsOptions, detail::jps_find_path_impl decl
#include "JpsCore.hpp"         // GridView + FindPathJPS (tuned overload)

#include <algorithm> // std::min
#include <cstddef>   // std::size_t
#include <cstdint>   // std::int64_t
#include <cstdlib>   // std::abs
#include <vector>    // std::vector

namespace colony::path::detail
{
    namespace jps_adapter_detail
    {
        static inline int sgn_int(int v) noexcept { return (v > 0) - (v < 0); }

        // One-step move legality in the same spirit as the core JPS implementation.
        static bool step_ok(const GridView& g,
                            int x, int y,
                            int dx, int dy,
                            bool allowDiagonal,
                            bool dontCrossCorners) noexcept
        {
            const int nx = x + dx;
            const int ny = y + dy;

            if (!g.passable(nx, ny))
                return false;

            if (dx != 0 && dy != 0)
            {
                if (!allowDiagonal)
                    return false;

                if (dontCrossCorners)
                {
                    // Disallow diagonal corner cutting.
                    if (!g.passable(x + dx, y) || !g.passable(x, y + dy))
                        return false;
                }
            }

            return true;
        }

        // Robust integer grid line-of-sight using an integer "grid-walk" / DDA traversal.
        //
        // Corner rule:
        // - If decision == 0 we cross a corner => diagonal step.
        // - If dontCrossCorners==true, diagonal requires BOTH side-adjacent cells to be passable.
        static bool line_of_sight_gridwalk(const GridView& g,
                                           colony::path::Cell a,
                                           colony::path::Cell b,
                                           bool allowDiagonal,
                                           bool dontCrossCorners) noexcept
        {
            if (!g.passable(a.x, a.y) || !g.passable(b.x, b.y))
                return false;

            int x = a.x;
            int y = a.y;

            const int dx = b.x - a.x;
            const int dy = b.y - a.y;

            const int nx = std::abs(dx);
            const int ny = std::abs(dy);

            // If diagonal movement is globally disabled, only axis-aligned LOS is allowed.
            if (!allowDiagonal && nx != 0 && ny != 0)
                return false;

            const int sx = sgn_int(dx);
            const int sy = sgn_int(dy);

            if (nx == 0 && ny == 0)
                return true;

            int ix = 0;
            int iy = 0;

            while (ix < nx || iy < ny)
            {
                // decision = (1+2*ix)*ny - (1+2*iy)*nx
                const std::int64_t decision =
                    (std::int64_t(1) + 2 * std::int64_t(ix)) * std::int64_t(ny) -
                    (std::int64_t(1) + 2 * std::int64_t(iy)) * std::int64_t(nx);

                if (decision == 0)
                {
                    // Corner crossing => diagonal step
                    if (!allowDiagonal)
                        return false;

                    if (dontCrossCorners)
                    {
                        if (!g.passable(x + sx, y) || !g.passable(x, y + sy))
                            return false;
                    }

                    x += sx;
                    y += sy;
                    ++ix;
                    ++iy;
                }
                else if (decision < 0)
                {
                    // x boundary first
                    x += sx;
                    ++ix;
                }
                else
                {
                    // y boundary first
                    y += sy;
                    ++iy;
                }

                if (!g.passable(x, y))
                    return false;
            }

            return true;
        }

        // Expand a jump-point path into a dense, step-by-step grid path.
        // This is safer for tests that validate corner-cutting per-step.
        static std::vector<colony::path::Cell>
        expand_jump_points(const GridView& g,
                           const std::vector<std::pair<int, int>>& jp,
                           bool allowDiagonal,
                           bool dontCrossCorners)
        {
            if (jp.empty())
                return {};

            std::vector<colony::path::Cell> out;
            out.reserve(jp.size()); // conservative; may grow if segments are long

            colony::path::Cell cur{ jp.front().first, jp.front().second };
            out.push_back(cur);

            for (std::size_t i = 1; i < jp.size(); ++i)
            {
                const colony::path::Cell goal{ jp[i].first, jp[i].second };

                const int sdx = sgn_int(goal.x - cur.x);
                const int sdy = sgn_int(goal.y - cur.y);

                // Defensive: should not happen, but avoids infinite loops.
                if (sdx == 0 && sdy == 0)
                    continue;

                // JPS jump points should be connected by straight runs in 4/8 dirs.
                // Step along until we reach goal.
                while (cur.x != goal.x || cur.y != goal.y)
                {
                    if (!step_ok(g, cur.x, cur.y, sdx, sdy, allowDiagonal, dontCrossCorners))
                        return {}; // inconsistent path or grid changed

                    cur.x += sdx;
                    cur.y += sdy;
                    out.push_back(cur);
                }
            }

            return out;
        }

        // Optional smoothing: bounded "farthest visible" string pulling.
        static std::vector<colony::path::Cell>
        smooth_string_pull(const GridView& g,
                           const std::vector<colony::path::Cell>& in,
                           bool allowDiagonal,
                           bool dontCrossCorners)
        {
            if (in.size() <= 2)
                return in;

            std::vector<colony::path::Cell> out;
            out.reserve(in.size());
            out.push_back(in.front());

            const bool fullSearch = (in.size() <= 256);
            constexpr std::size_t LOOKAHEAD = 96;

            std::size_t i = 0;
            while (i + 1 < in.size())
            {
                std::size_t j = in.size() - 1;
                if (!fullSearch)
                    j = std::min(in.size() - 1, i + LOOKAHEAD);

                for (; j > i + 1; --j)
                {
                    if (line_of_sight_gridwalk(g, in[i], in[j], allowDiagonal, dontCrossCorners))
                        break;
                }

                if (j <= i)
                    j = i + 1;

                out.push_back(in[j]);
                i = j;
            }

            if (out.empty() || out.back().x != in.back().x || out.back().y != in.back().y)
                out.push_back(in.back());

            return out;
        }
    } // namespace jps_adapter_detail

    std::vector<colony::path::Cell>
    jps_find_path_impl(const IGrid& grid,
                       colony::path::Cell start,
                       colony::path::Cell goal,
                       const JpsOptions& opt)
    {
        colony::path::GridView view{};
        view.width = grid.width();
        view.height = grid.height();
        view.isBlocked = [&grid](int x, int y) -> bool
        {
            // IGrid::walkable == inside && traversable
            // GridView expects isBlocked == true when not traversable.
            return !grid.walkable(x, y);
        };

        // Use the tuned overload so costStraight/costDiagonal/heuristicWeight/tieBreakCross
        // actually drive the algorithm.
        const auto jumpPoints = colony::path::FindPathJPS(
            view,
            start.x, start.y,
            goal.x, goal.y,
            opt.allowDiagonal,
            opt.dontCrossCorners,
            opt.costStraight,
            opt.costDiagonal,
            opt.heuristicWeight,
            opt.tieBreakCross);

        // Expand to a dense per-step path (more compatible with typical grid APIs/tests).
        auto out = jps_adapter_detail::expand_jump_points(
            view, jumpPoints, opt.allowDiagonal, opt.dontCrossCorners);

        // Optional string pulling smoothing (returns waypoints).
        if (opt.smoothPath && !out.empty())
        {
            out = jps_adapter_detail::smooth_string_pull(
                view, out, opt.allowDiagonal, opt.dontCrossCorners);
        }

        return out;
    }

} // namespace colony::path::detail
