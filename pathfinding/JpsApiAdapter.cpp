// pathfinding/JpsApiAdapter.cpp
// Bridges the public API in include/pathfinding/Jps.hpp to the existing GridView JPS core.

#include "pathfinding/Jps.hpp"  // Cell, IGrid, JpsOptions, wrapper decl
#include "JpsCore.hpp"          // GridView + FindPathJPS (7-arg overload)

#include <utility>  // std::pair
#include <vector>   // std::vector

namespace colony::path::detail
{
    std::vector<Cell> jps_find_path_impl(const IGrid& grid,
                                         Cell start,
                                         Cell goal,
                                         const JpsOptions& opt)
    {
        // Adapt IGrid -> GridView
        GridView gv;
        gv.width  = grid.width();
        gv.height = grid.height();

        // GridView expects "isBlocked"; IGrid exposes "walkable"
        gv.isBlocked = [&](int x, int y) -> bool
        {
            return !grid.walkable(x, y);
        };

        // Use the existing, tested JPS core implementation.
        // IMPORTANT: plumb allowDiagonal + dontCrossCorners (unit tests depend on this).
        const auto pts = ::colony::path::FindPathJPS(
            gv,
            start.x, start.y,
            goal.x, goal.y,
            opt.allowDiagonal,
            opt.dontCrossCorners
        );

        std::vector<Cell> out;
        out.reserve(pts.size());
        for (const auto& p : pts)
        {
            out.push_back(Cell{ p.first, p.second });
        }

        // Optional hook: opt.smoothPath (not required for current tests).
        // If you later add smoothing, do it here so the public API stays stable.

        return out;
    }
} // namespace colony::path::detail
