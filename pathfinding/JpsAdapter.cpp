// pathfinding/JpsAdapter.cpp
//
// Bridges the public API in include/pathfinding/Jps.hpp
// to the low-level core in pathfinding/JpsCore.hpp / pathfinding/Jps.cpp.

#include "pathfinding/Jps.hpp"
#include "pathfinding/JpsCore.hpp"

#include <utility>
#include <vector>

namespace colony::path::detail
{
    std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
    {
        GridView view{};
        view.width  = grid.width();
        view.height = grid.height();
        view.isBlocked = [&grid](int x, int y) -> bool
        {
            return !grid.walkable(x, y);
        };

        // Forward ALL relevant options into the core.
        const auto pathXY = FindPathJPS(view,
                                        start.x, start.y,
                                        goal.x, goal.y,
                                        opt.allowDiagonal,
                                        opt.dontCrossCorners,
                                        opt.costStraight,
                                        opt.costDiagonal,
                                        opt.heuristicWeight);

        std::vector<Cell> out;
        out.reserve(pathXY.size());
        for (const auto& [x, y] : pathXY)
            out.push_back(Cell{ x, y });

        // (Optional future step): if (opt.smoothPath) { ... string-pull ... }
        return out;
    }
} // namespace colony::path::detail
