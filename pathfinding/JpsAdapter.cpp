// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp (IGrid-based API) to pathfinding/JpsCore.hpp (GridView-based JPS).
//
// This version also implements "smart/faster" smoothPath:
// - Robust line-of-sight via integer grid-walk (DDA-like) traversal
// - Bounded lookahead smoothing for long paths (keeps cost predictable)

#include "pathfinding/Jps.hpp" // IGrid, Cell, JpsOptions, detail::jps_find_path_impl decl
#include "JpsCore.hpp"         // GridView, FindPathJPS (tuned overload)

#include <algorithm> // std::min
#include <cstddef>   // std::size_t
#include <cstdint>   // std::int64_t
#include <cstdlib>   // std::abs
#include <vector>    // std::vector

namespace colony::path::detail
{
    namespace jps_adapter_detail
    {
        static inline int sgn_int(int v) noexcept
        {
            return (v > 0) - (v < 0);
        }

        // Robust grid line-of-sight using an integer "grid-walk" / DDA traversal:
        // Decide whether the next boundary crossed is vertical or horizontal by comparing
        // (0.5+ix)/nx vs (0.5+iy)/ny, rewritten into integer form.
        //
        // This follows the same idea as voxel traversal (tMaxX/tMaxY) in Amanatides & Woo
        // but in an integer-friendly form suitable for MSVC/unity builds.
        //
        // Corner rule:
        // - If decision == 0 we are crossing a grid corner (diagonal step).
        // - If dontCrossCorners==true we require BOTH side-adjacent cells to be passable.
        static bool line_of_sight_gridwalk(const GridView& g,
                                           Cell a,
                                           Cell b,
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

            // If diagonal segments are not allowed, only allow axis-aligned LOS.
            if (!allowDiagonal && nx != 0 && ny != 0)
                return false;

            const int sx = sgn_int(dx);
            const int sy = sgn_int(dy);

            // Start == goal is trivially visible (already ensured passable).
            if (nx == 0 && ny == 0)
                return true;

            int ix = 0;
            int iy = 0;

            // We step from cell center to cell center, counting boundary crossings.
            // decision = (1+2*ix)*ny - (1+2*iy)*nx
            // - decision < 0  => next step crosses a vertical boundary (move in x)
            // - decision > 0  => next step crosses a horizontal boundary (move in y)
            // - decision == 0 => crosses a corner (move diagonally)
            while (ix < nx || iy < ny)
            {
                const std::int64_t decision =
                    (std::int64_t(1) + 2 * std::int64_t(ix)) * std::int64_t(ny) -
                    (std::int64_t(1) + 2 * std::int64_t(iy)) * std::int64_t(nx);

                if (decision == 0)
                {
                    // Corner crossing: diagonal step.
                    if (!allowDiagonal)
                        return false;

                    if (dontCrossCorners)
                    {
                        // Block diagonal corner cutting: both adjacent cardinals must be open.
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
                    // Horizontal boundary next -> x step
                    x += sx;
                    ++ix;
                }
                else
                {
                    // Vertical boundary next -> y step
                    y += sy;
                    ++iy;
                }

                if (!g.passable(x, y))
                    return false;
            }

            return true;
        }

        static std::vector<Cell> smooth_string_pull(const GridView& g,
                                                    const std::vector<Cell>& in,
                                                    bool allowDiagonal,
                                                    bool dontCrossCorners)
        {
            if (in.size() <= 2)
                return in;

            std::vector<Cell> out;
            out.reserve(in.size());
            out.push_back(in.front());

            // Strategy:
            // - For small paths, do the classic "farthest visible" search (best quality).
            // - For long paths, bound how far we look ahead to keep smoothing time predictable.
            //
            // This avoids worst-case O(n^2) LOS checks on long paths.
            const bool fullSearch = (in.size() <= 256);

            // Tuneable constant:
            // Higher => more smoothing quality but more LOS checks on very long paths.
            constexpr std::size_t LOOKAHEAD = 96;

            std::size_t i = 0;
            while (i + 1 < in.size())
            {
                std::size_t j = in.size() - 1;
                if (!fullSearch)
                    j = std::min(in.size() - 1, i + LOOKAHEAD);

                // Find the farthest visible point from anchor i (within the chosen range).
                for (; j > i + 1; --j)
                {
                    if (line_of_sight_gridwalk(g, in[i], in[j], allowDiagonal, dontCrossCorners))
                        break;
                }

                // Defensive (shouldn’t happen, but avoids infinite loops).
                if (j <= i)
                    j = i + 1;

                out.push_back(in[j]);
                i = j;
            }

            // Ensure the goal is present (avoid any edge cases where it might be missed).
            if (out.empty() || out.back().x != in.back().x || out.back().y != in.back().y)
                out.push_back(in.back());

            return out;
        }
    } // namespace jps_adapter_detail

    std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
    {
        // Adapt IGrid -> GridView expected by the JPS core.
        colony::path::GridView view{};
        view.width = grid.width();
        view.height = grid.height();
        view.isBlocked = [&grid](int x, int y) -> bool {
            // IGrid::walkable == inside && traversable
            // GridView expects isBlocked == true when not traversable.
            return !grid.walkable(x, y);
        };

        // IMPORTANT:
        // Use the tuned overload so JpsOptions.costStraight/costDiagonal/heuristicWeight/tieBreakCross
        // actually drive the algorithm.
        const auto pathPairs = colony::path::FindPathJPS(
            view,
            start.x, start.y,
            goal.x, goal.y,
            opt.allowDiagonal,
            opt.dontCrossCorners,
            opt.costStraight,
            opt.costDiagonal,
            opt.heuristicWeight,
            opt.tieBreakCross);

        // Convert to Cell path.
        std::vector<Cell> out;
        out.reserve(pathPairs.size());
        for (const auto& xy : pathPairs)
            out.push_back(Cell{xy.first, xy.second});

        // Optional smoothing (string-pulling).
        if (opt.smoothPath)
        {
            out = jps_adapter_detail::smooth_string_pull(
                view, out,
                opt.allowDiagonal,
                opt.dontCrossCorners);
        }

        return out;
    }
} // namespace colony::path::detail
