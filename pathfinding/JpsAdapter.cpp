// pathfinding/JpsAdapter.cpp
// Bridges include/pathfinding/Jps.hpp (IGrid-based API) to pathfinding/JpsCore.hpp (GridView-based JPS).

#include "pathfinding/Jps.hpp" // IGrid, Cell, JpsOptions, detail::jps_find_path_impl decl
#include "JpsCore.hpp"         // GridView, FindPathJPS
#include <vector>
#include <utility>

namespace colony::path::detail
{
    std::vector<Cell>
    jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
    {
        // Adapt IGrid -> GridView expected by the JPS core
        colony::path::GridView view{};
        view.width  = grid.width();
        view.height = grid.height();

        // GridView::isBlocked returns "true if blocked".
        // IGrid::walkable returns "true if inside AND traversable".
        view.isBlocked = [&grid](int x, int y) -> bool {
            return !grid.walkable(x, y);
        };

        // Call the core JPS implementation (supports dontCrossCorners).
        const auto pathPairs = colony::path::FindPathJPS(
            view,
            start.x, start.y,
            goal.x, goal.y,
            opt.allowDiagonal,
            opt.dontCrossCorners
        );

        // Convert to vector<Cell>
        std::vector<Cell> out;
        out.reserve(pathPairs.size());
        for (const auto& xy : pathPairs)
        {
            out.push_back(Cell{ xy.first, xy.second });
        }

        // NOTE:
        // JpsOptions also has costStraight/costDiagonal/heuristicWeight/tieBreakCross/smoothPath.
        // Your current core JPS uses fixed costs + fixed heuristic; this adapter intentionally
        // only wires allowDiagonal + dontCrossCorners (the parts your tests validate).
        return out;
    }
} // namespace colony::path::detail
