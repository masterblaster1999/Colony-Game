#pragma once
// JpsCore.hpp — minimal, self-contained Jump Point Search interface for MSVC
//
// NOTE (compatibility):
// - This header declares a 7-argument FindPathJPS overload that includes `dontCrossCorners`.
// - It also provides a 6-argument *inline* back-compat overload that forwards
//   to the 7-argument version with dontCrossCorners=true.
//
// NEW (costs/weight):
// - A 10-argument overload is provided so higher-level APIs (e.g. JpsOptions)
//   can drive straight/diagonal movement costs and heuristic weighting.

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace colony::path
{
    // Lightweight view into a grid.
    // You provide dimensions and a callback that returns true for blocked cells.
    struct GridView
    {
        int width = 0;
        int height = 0;
        std::function<bool(int, int)> isBlocked;

        bool inBounds(int x, int y) const noexcept
        {
            return (x >= 0 && x < width && y >= 0 && y < height);
        }

        bool passable(int x, int y) const noexcept
        {
            return inBounds(x, y) && !isBlocked(x, y);
        }
    };

    // Node used for A* frontier; stores parent to reconstruct the path.
    struct Node
    {
        int x = 0, y = 0;
        float g = 0.f, h = 0.f;
        int px = -1, py = -1;
    };

    // Public API: Find a grid path from (sx,sy) to (gx,gy).
    // Returns a polyline of (x,y) tiles including start and goal.
    //
    // allowDiagonal:
    // - true => 8-neighborhood
    // - false => 4-neighborhood (implementation may fall back to plain A*)
    //
    // dontCrossCorners (only meaningful when allowDiagonal == true):
    // - true => forbid corner cutting on diagonals (requires both adjacent cardinals free)
    // - false => allow diagonal moves even if they "cut" past a blocked corner
    //
    // costStraight / costDiagonal:
    // - per-step costs for cardinal vs diagonal moves.
    //
    // heuristicWeight:
    // - 1.0 => classic A* (optimal with admissible heuristic)
    // - >1.0 => weighted A* (often faster, may be suboptimal)
    // - 0.0 => Dijkstra (no heuristic)
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners,
                float costStraight,
                float costDiagonal,
                float heuristicWeight);

    // Back-compat: original 7-arg core call (defaults to classic A* costs/weight).
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners);

    // Back-compat overload: preserves existing call sites that only specify allowDiagonal.
    // Equivalent to: FindPathJPS(..., allowDiagonal, /*dontCrossCorners=*/true)
    inline std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal = true)
    {
        return FindPathJPS(grid, sx, sy, gx, gy, allowDiagonal, /*dontCrossCorners=*/true);
    }

    // ---- Internals ----

    float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept;

    std::optional<std::pair<int, int>>
    Jump(const GridView& grid,
         int x, int y,
         int dx, int dy,
         int goalX, int goalY,
         bool allowDiagonal);

    void PruneNeighbors(const GridView& grid,
                        int x, int y,
                        int dx, int dy,
                        bool allowDiagonal,
                        std::vector<std::pair<int, int>>& outDirs);

    std::vector<std::pair<int, int>>
    Reconstruct(const std::unordered_map<std::uint64_t, std::pair<int, int>>& parent,
                int sx, int sy,
                int gx, int gy);

    inline std::uint64_t Pack(int x, int y) noexcept
    {
        return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y);
    }

} // namespace colony::path
