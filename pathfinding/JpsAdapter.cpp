// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp (IGrid-based API) to pathfinding/JpsCore.hpp (GridView-based core).

#include "pathfinding/Jps.hpp" // IGrid, Cell, JpsOptions, detail::jps_find_path_impl decl
#include "JpsCore.hpp"         // GridView, FindPathJPS

#include <cstddef> // size_t
#include <cstdlib> // abs
#include <vector>

namespace colony::path::detail
{
    // NOTE (Unity/Jumbo builds):
    // Avoid anonymous-namespace helpers with generic names (canStep, blocked, etc).
    // Those can collide when CMake unity merges multiple .cpp files into one TU.
    namespace jps_adapter_detail
    {
        static inline bool jps_adapter_step_ok(const colony::path::GridView& g,
                                               int x, int y,
                                               int nx, int ny,
                                               bool allowDiagonal,
                                               bool dontCrossCorners) noexcept
        {
            const int dx = nx - x;
            const int dy = ny - y;

            if (dx == 0 && dy == 0)
                return false;

            if (!g.passable(nx, ny))
                return false;

            if (dx != 0 && dy != 0)
            {
                if (!allowDiagonal)
                    return false;

                if (dontCrossCorners)
                {
                    if (!g.passable(x + dx, y) || !g.passable(x, y + dy))
                        return false;
                }
            }

            return true;
        }

        // Bresenham-style grid LOS between two cells (inclusive).
        static bool jps_adapter_line_of_sight(const colony::path::GridView& g,
                                              colony::path::Cell a,
                                              colony::path::Cell b,
                                              bool allowDiagonal,
                                              bool dontCrossCorners) noexcept
        {
            // If diagonals are not allowed, only smooth axis-aligned segments.
            if (!allowDiagonal && (a.x != b.x) && (a.y != b.y))
                return false;

            int x0 = a.x, y0 = a.y;
            const int x1 = b.x, y1 = b.y;

            const int dx = std::abs(x1 - x0);
            const int dy = std::abs(y1 - y0);

            const int sx = (x0 < x1) ? 1 : -1;
            const int sy = (y0 < y1) ? 1 : -1;

            int err = dx - dy;

            while (!(x0 == x1 && y0 == y1))
            {
                int e2 = err * 2;

                int nx = x0;
                int ny = y0;

                if (e2 > -dy) { err -= dy; nx += sx; }
                if (e2 <  dx) { err += dx; ny += sy; }

                if (!jps_adapter_step_ok(g, x0, y0, nx, ny, allowDiagonal, dontCrossCorners))
                    return false;

                x0 = nx;
                y0 = ny;
            }

            return true;
        }

        // Simple (safe) string-pulling: keep farthest visible waypoint each step.
        static std::vector<colony::path::Cell>
        jps_adapter_string_pull(const colony::path::GridView& g,
                                const std::vector<colony::path::Cell>& path,
                                bool allowDiagonal,
                                bool dontCrossCorners)
        {
            if (path.size() < 3)
                return path;

            std::vector<colony::path::Cell> out;
            out.reserve(path.size());

            std::size_t i = 0;
            out.push_back(path.front());

            while (i + 1 < path.size())
            {
                std::size_t best = i + 1; // always can at least take the next point

                // Find farthest point visible from i.
                for (std::size_t j = path.size() - 1; j > i; --j)
                {
                    if (jps_adapter_line_of_sight(g, path[i], path[j], allowDiagonal, dontCrossCorners))
                    {
                        best = j;
                        break;
                    }
                }

                out.push_back(path[best]);
                i = best;
            }

            return out;
        }
    } // namespace jps_adapter_detail

    std::vector<Cell>
    jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
    {
        // Adapt IGrid -> GridView expected by the core
        colony::path::GridView view{};
        view.width = grid.width();
        view.height = grid.height();

        // GridView::isBlocked returns "true if blocked".
        // IGrid::walkable returns "true if inside AND traversable".
        view.isBlocked = [&grid](int x, int y) -> bool {
            return !grid.walkable(x, y);
        };

        // Call tuned core: costs, heuristicWeight, tieBreakCross all drive search now.
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

        std::vector<Cell> out;
        out.reserve(pathPairs.size());
        for (const auto& xy : pathPairs)
            out.push_back(Cell{ xy.first, xy.second });

        // Optional post-smoothing (string pulling).
        // This respects allowDiagonal + dontCrossCorners via the LOS test.
        if (opt.smoothPath)
            out = jps_adapter_detail::jps_adapter_string_pull(view, out, opt.allowDiagonal, opt.dontCrossCorners);

        return out;
    }

} // namespace colony::path::detail
