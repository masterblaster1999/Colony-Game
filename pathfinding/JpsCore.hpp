#pragma once
// JpsCore.hpp — minimal, self-contained Jump Point Search core for MSVC/Windows.
//
// IMPORTANT (Unity/Jumbo builds + MSVC):
// - The 6-arg FindPathJPS overload is provided INLINE below.
//   Do NOT provide an out-of-line definition of the 6-arg overload in any .cpp,
//   or MSVC will error with "function already has a body" when unity builds merge TUs.
//
// This header exposes:
// - A base API: FindPathJPS(grid, sx,sy, gx,gy, allowDiagonal, dontCrossCorners)
// - A tuned API: FindPathJPS(..., costStraight, costDiagonal, heuristicWeight, tieBreakCross)

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace colony::path
{
    // Lightweight view into a grid. You provide dimensions and a callback
    // that returns true for blocked cells (walls) and false for free cells.
    struct GridView
    {
        int width = 0;
        int height = 0;

        // Return true if blocked.
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

    // Node type retained for compatibility / potential tests.
    struct Node
    {
        int x = 0, y = 0;
        float g = 0.f, h = 0.f;
        int px = -1, py = -1;
    };

    // Pack (x,y) into a 64-bit key.
    inline std::uint64_t Pack(int x, int y) noexcept
    {
        return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y);
    }

    // -------------------------------------------------------------------------
    // Public API (base)
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Public API (tuned) — lets JpsOptions drive the algorithm
    // -------------------------------------------------------------------------
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners,
                float costStraight,
                float costDiagonal,
                float heuristicWeight,
                bool tieBreakCross);

    // Convenience inline: default tieBreakCross=false if you call the tuned overload directly.
    inline std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners,
                float costStraight,
                float costDiagonal,
                float heuristicWeight = 1.0f)
    {
        return FindPathJPS(grid,
                           sx, sy, gx, gy,
                           allowDiagonal, dontCrossCorners,
                           costStraight, costDiagonal, heuristicWeight,
                           /*tieBreakCross=*/false);
    }

    // -------------------------------------------------------------------------
    // Internals (declared for reuse/tests)
    // -------------------------------------------------------------------------
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
                int sx, int sy, int gx, int gy);

} // namespace colony::path
