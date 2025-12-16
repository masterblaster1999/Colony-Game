// Jps.cpp — MSVC-friendly implementation of Jump Point Search
#include "JpsCore.hpp"

#include <algorithm>     // min, reverse
#include <cstddef>       // size_t
#include <cstdint>       // uint64_t
#include <cstdlib>       // abs
#include <optional>      // optional
#include <queue>         // priority_queue
#include <unordered_map> // unordered_map
#include <utility>       // pair
#include <vector>        // vector

namespace colony::path
{
    namespace
    {
        static inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

        // Treat OOB as blocked for forced-neighbor tests
        static inline bool blocked(const GridView& g, int x, int y) noexcept
        {
            return !g.inBounds(x, y) || g.isBlocked(x, y);
        }

        // One-step move legality from (x,y) in direction (dx,dy).
        // - If dx/dy is diagonal:
        //   - requires allowDiag
        //   - if dontCrossCorners: both adjacent cardinals must be passable.
        static inline bool canStep(const GridView& g,
                                   int x, int y,
                                   int dx, int dy,
                                   bool allowDiag,
                                   bool dontCrossCorners) noexcept
        {
            if (dx == 0 && dy == 0)
                return false;

            const int nx = x + dx;
            const int ny = y + dy;

            if (!g.passable(nx, ny))
                return false;

            if (dx != 0 && dy != 0)
            {
                if (!allowDiag)
                    return false;

                if (dontCrossCorners)
                {
                    // Prevent diagonal corner cutting: both side-adjacent cells must be open.
                    if (!g.passable(x + dx, y) || !g.passable(x, y + dy))
                        return false;
                }
            }

            return true;
        }
    } // namespace

    float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept
    {
        const int dx = std::abs(x1 - x0);
        const int dy = std::abs(y1 - y0);

        if (!allowDiagonal) // Manhattan
            return float(dx + dy);

        // Octile distance: cost(diagonal)=sqrt(2), straight=1
        const float D  = 1.0f;
        const float D2 = 1.41421356237f;
        return D * float(dx + dy) + (D2 - 2.0f * D) * float(std::min(dx, dy));
    }

    // ---- Jump Point Search core (extended overload supports dontCrossCorners) ----

    std::optional<std::pair<int, int>>
    Jump(const GridView& g,
         int x, int y,
         int dx, int dy,
         int goalX, int goalY,
         bool allowDiag,
         bool dontCrossCorners)
    {
        if (dx == 0 && dy == 0)
            return std::nullopt;

        // If diagonal movement is disabled globally, diagonal directions are invalid.
        if (!allowDiag && dx != 0 && dy != 0)
            return std::nullopt;

        for (;;)
        {
            // Step once in (dx,dy)
            if (!canStep(g, x, y, dx, dy, allowDiag, dontCrossCorners))
                return std::nullopt;

            x += dx;
            y += dy;

            // Goal reached
            if (x == goalX && y == goalY)
                return std::make_pair(x, y);

            // Forced-neighbor checks (Harabor & Grastien style)
            if (dx != 0 && dy != 0) // diagonal move
            {
                // Forced neighbors for diagonal movement:
                // - if one of the orthogonal neighbors is blocked
                //   and the corresponding diagonal "around the corner" is open.
                //
                // Note: For dontCrossCorners=true, such forced diagonal successors may become invalid,
                // and will be filtered by canStep() during pruning/expansion.
                if ((blocked(g, x - dx, y) && g.passable(x - dx, y + dy)) ||
                    (blocked(g, x, y - dy) && g.passable(x + dx, y - dy)))
                {
                    return std::make_pair(x, y);
                }

                // When moving diagonally, also check for jump points in each cardinal direction.
                if (Jump(g, x, y, dx, 0, goalX, goalY, allowDiag, dontCrossCorners).has_value() ||
                    Jump(g, x, y, 0, dy, goalX, goalY, allowDiag, dontCrossCorners).has_value())
                {
                    return std::make_pair(x, y);
                }

                // Continue stepping diagonally (loop tail)
            }
            else if (dx != 0) // horizontal
            {
                if ((blocked(g, x, y + 1) && g.passable(x + dx, y + 1)) ||
                    (blocked(g, x, y - 1) && g.passable(x + dx, y - 1)))
                {
                    return std::make_pair(x, y);
                }

                // Continue stepping horizontally (loop tail)
            }
            else // vertical (dy != 0)
            {
                if ((blocked(g, x + 1, y) && g.passable(x + 1, y + dy)) ||
                    (blocked(g, x - 1, y) && g.passable(x - 1, y + dy)))
                {
                    return std::make_pair(x, y);
                }

                // Continue stepping vertically (loop tail)
            }
        }
    }

    // Back-compat signature from JpsCore.hpp (defaults to dontCrossCorners=true).
    std::optional<std::pair<int, int>>
    Jump(const GridView& g,
         int x, int y,
         int dx, int dy,
         int goalX, int goalY,
         bool allowDiag)
    {
        return Jump(g, x, y, dx, dy, goalX, goalY, allowDiag, /*dontCrossCorners=*/true);
    }

    void PruneNeighbors(const GridView& g,
                        int x, int y,
                        int dx, int dy,
                        bool allowDiag,
                        bool dontCrossCorners,
                        std::vector<std::pair<int, int>>& outDirs)
    {
        outDirs.clear();

        auto pushDirIfFirstStepValid = [&](int ndx, int ndy)
        {
            if (canStep(g, x, y, ndx, ndy, allowDiag, dontCrossCorners))
                outDirs.emplace_back(ndx, ndy);
        };

        if (dx == 0 && dy == 0)
        {
            // Start node: include all valid neighbors (4 or 8)
            pushDirIfFirstStepValid(1, 0);
            pushDirIfFirstStepValid(-1, 0);
            pushDirIfFirstStepValid(0, 1);
            pushDirIfFirstStepValid(0, -1);

            if (allowDiag)
            {
                pushDirIfFirstStepValid(1, 1);
                pushDirIfFirstStepValid(-1, 1);
                pushDirIfFirstStepValid(1, -1);
                pushDirIfFirstStepValid(-1, -1);
            }
            return;
        }

        // Moving direction (dx,dy) from parent -> current
        if (dx != 0 && dy != 0) // diagonal move
        {
            // Natural neighbors
            pushDirIfFirstStepValid(dx, 0);
            pushDirIfFirstStepValid(0, dy);
            pushDirIfFirstStepValid(dx, dy);

            // Forced neighbors (diagonal variants)
            if (blocked(g, x - dx, y) && g.passable(x - dx, y + dy))
                pushDirIfFirstStepValid(-dx, dy);
            if (blocked(g, x, y - dy) && g.passable(x + dx, y - dy))
                pushDirIfFirstStepValid(dx, -dy);
        }
        else if (dx != 0) // horizontal
        {
            // Natural neighbor
            pushDirIfFirstStepValid(dx, 0);

            // Forced neighbors (diagonal around obstacle)
            if (allowDiag)
            {
                if (blocked(g, x, y + 1) && g.passable(x + dx, y + 1))
                    pushDirIfFirstStepValid(dx, 1);
                if (blocked(g, x, y - 1) && g.passable(x + dx, y - 1))
                    pushDirIfFirstStepValid(dx, -1);
            }
        }
        else // vertical (dy != 0)
        {
            // Natural neighbor
            pushDirIfFirstStepValid(0, dy);

            // Forced neighbors (diagonal around obstacle)
            if (allowDiag)
            {
                if (blocked(g, x + 1, y) && g.passable(x + 1, y + dy))
                    pushDirIfFirstStepValid(1, dy);
                if (blocked(g, x - 1, y) && g.passable(x - 1, y + dy))
                    pushDirIfFirstStepValid(-1, dy);
            }
        }
    }

    // Back-compat signature from JpsCore.hpp (defaults to dontCrossCorners=true).
    void PruneNeighbors(const GridView& g,
                        int x, int y,
                        int dx, int dy,
                        bool allowDiag,
                        std::vector<std::pair<int, int>>& outDirs)
    {
        PruneNeighbors(g, x, y, dx, dy, allowDiag, /*dontCrossCorners=*/true, outDirs);
    }

    std::vector<std::pair<int, int>>
    Reconstruct(const std::unordered_map<std::uint64_t, std::pair<int, int>>& parent,
                int sx, int sy, int gx, int gy)
    {
        std::vector<std::pair<int, int>> path;

        int x = gx, y = gy;
        while (!(x == sx && y == sy))
        {
            path.emplace_back(x, y);

            const auto it = parent.find(Pack(x, y));
            if (it == parent.end())
            {
                path.clear(); // no path
                return path;
            }

            x = it->second.first;
            y = it->second.second;
        }

        path.emplace_back(sx, sy);
        std::reverse(path.begin(), path.end());
        return path;
    }

    // Extended overload: supports dontCrossCorners explicitly.
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners)
    {
        // Hard correctness guards (helps unit tests & prevents weird behavior)
        if (!grid.passable(sx, sy) || !grid.passable(gx, gy))
            return {};

        if (sx == gx && sy == gy)
            return { {sx, sy} };

        struct PQItem
        {
            int x{}, y{};
            float f{}, g{};
            int px{}, py{}; // parent (for direction pruning)

            bool operator<(const PQItem& o) const noexcept
            {
                // std::priority_queue is a max-heap by default; invert for min-heap by f.
                return f > o.f;
            }
        };

        std::priority_queue<PQItem> open;
        std::unordered_map<std::uint64_t, float> gScore;
        std::unordered_map<std::uint64_t, std::pair<int, int>> parent;

        // Light reserve to reduce rehashing on typical grids (safe on Windows/MSVC).
        if (grid.width > 0 && grid.height > 0)
        {
            const std::size_t cap = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
            gScore.reserve(cap);
            parent.reserve(cap);
        }

        const auto startKey = Pack(sx, sy);
        gScore[startKey] = 0.0f;

        open.push(PQItem{ sx, sy, Octile(sx, sy, gx, gy, allowDiagonal), 0.0f, sx, sy });

        std::vector<std::pair<int, int>> dirs;
        dirs.reserve(8);

        while (!open.empty())
        {
            const PQItem cur = open.top();
            open.pop();

            // Skip stale queue entries (critical for correctness with JPS pruning based on parent direction).
            const auto curKey = Pack(cur.x, cur.y);
            const auto bestIt = gScore.find(curKey);
            if (bestIt != gScore.end() && cur.g > bestIt->second)
                continue;

            if (cur.x == gx && cur.y == gy)
                return Reconstruct(parent, sx, sy, gx, gy);

            PruneNeighbors(grid,
                           cur.x, cur.y,
                           sgn(cur.x - cur.px), sgn(cur.y - cur.py),
                           allowDiagonal, dontCrossCorners,
                           dirs);

            for (const auto& [dx, dy] : dirs)
            {
                auto jp = Jump(grid, cur.x, cur.y, dx, dy, gx, gy, allowDiagonal, dontCrossCorners);
                if (!jp)
                    continue;

                const int jx = jp->first;
                const int jy = jp->second;
                const auto key = Pack(jx, jy);

                const float newG = cur.g + Octile(cur.x, cur.y, jx, jy, allowDiagonal);

                const auto it = gScore.find(key);
                if (it == gScore.end() || newG < it->second)
                {
                    gScore[key] = newG;
                    parent[key] = { cur.x, cur.y };

                    const float f = newG + Octile(jx, jy, gx, gy, allowDiagonal);
                    open.push(PQItem{ jx, jy, f, newG, cur.x, cur.y });
                }
            }
        }

        return {}; // no path
    }

    // Back-compat signature from JpsCore.hpp (defaults to dontCrossCorners=true).
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal)
    {
        return FindPathJPS(grid, sx, sy, gx, gy, allowDiagonal, /*dontCrossCorners=*/true);
    }

} // namespace colony::path
