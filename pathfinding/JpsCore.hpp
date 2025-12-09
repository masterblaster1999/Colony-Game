#pragma once
// JpsCore.hpp — minimal, self-contained Jump Point Search interface for MSVC

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <queue>
#include <unordered_map>
#include <limits>
#include <cmath>

namespace colony::path
{
    // Lightweight view into a grid. You provide dimensions and a callback that
    // returns true for blocked cells (walls) and false for free cells.
    struct GridView
    {
        int width  = 0;
        int height = 0;
        std::function<bool(int /*x*/, int /*y*/)> isBlocked;

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
        float g = 0.f, h = 0.f; // cost-so-far and heuristic
        int px = -1, py = -1;   // parent
    };

    // Public API: Find a grid path from (sx,sy) to (gx,gy).
    // Returns a polyline of (x,y) tiles including start and goal.
    // Set allowDiagonal=false to restrict to 4-neighborhood.
    std::vector<std::pair<int,int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal = true);

    // ---- Internals (exposed for unit tests, but you can keep them header-only
    // to help MSVC optimize) ----

    // Heuristic: octile distance (works for 8-connected grids; reduces to
    // Manhattan if allowDiagonal is false).
    float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept;

    // Jump in direction (dx,dy) starting from (x,y). Returns the next "jump
    // point" (forced neighbor, or goal) or std::nullopt if blocked.
    std::optional<std::pair<int,int>>
    Jump(const GridView& grid, int x, int y, int dx, int dy,
         int goalX, int goalY, bool allowDiagonal);

    // Generate pruned neighbors for JPS given the parent direction (dx,dy).
    void PruneNeighbors(const GridView& grid, int x, int y,
                        int dx, int dy, bool allowDiagonal,
                        std::vector<std::pair<int,int>>& outDirs);

    // Utility to reconstruct final path from came-from map.
    std::vector<std::pair<int,int>>
    Reconstruct(const std::unordered_map<std::uint64_t, std::pair<int,int>>& parent,
                int sx, int sy, int gx, int gy);

    // Small helpers for keying (x,y) into unordered_map
    inline std::uint64_t Pack(int x, int y) noexcept
    {
        return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y);
    }
} // namespace colony::path
