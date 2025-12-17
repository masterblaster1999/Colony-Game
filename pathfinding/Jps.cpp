// Jps.cpp — MSVC-friendly implementation of Jump Point Search
#include "JpsCore.hpp"

#include <algorithm>     // min, max, reverse
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

        static inline float sanitizePositive(float v, float fallback) noexcept
        {
            // Handles <= 0 and NaN (since NaN > 0 is false).
            return (v > 0.0f) ? v : fallback;
        }

        static inline float heuristic_cost(int x0, int y0,
                                           int x1, int y1,
                                           bool allowDiagonal,
                                           float costStraight,
                                           float costDiagonal) noexcept
        {
            const int dx = std::abs(x1 - x0);
            const int dy = std::abs(y1 - y0);

            if (!allowDiagonal)
                return costStraight * float(dx + dy);

            const int mn = std::min(dx, dy);
            const int mx = std::max(dx, dy);

            // In an empty grid, a diagonal is only beneficial if cheaper than two straights.
            const float diag = (costDiagonal < 2.0f * costStraight) ? costDiagonal : (2.0f * costStraight);

            return diag * float(mn) + costStraight * float(mx - mn);
        }

        static inline float segment_cost(int x0, int y0,
                                         int x1, int y1,
                                         float costStraight,
                                         float costDiagonal) noexcept
        {
            const int dx = std::abs(x1 - x0);
            const int dy = std::abs(y1 - y0);

            if (dx == 0 || dy == 0)
                return costStraight * float(dx + dy);

            // For a proper JPS jump, dx == dy; but keep robust for safety.
            const int mn = std::min(dx, dy);
            const int mx = std::max(dx, dy);
            return costDiagonal * float(mn) + costStraight * float(mx - mn);
        }

        // Plain A* fallback used when JPS8 pruning assumptions don't hold.
        static std::vector<std::pair<int, int>>
        FindPathAStarFallback(const GridView& grid,
                              int sx, int sy,
                              int gx, int gy,
                              bool allowDiagonal,
                              bool dontCrossCorners,
                              float costStraight,
                              float costDiagonal,
                              float heuristicWeight)
        {
            struct PQItem
            {
                int x{}, y{};
                float f{}, g{};

                bool operator<(const PQItem& o) const noexcept
                {
                    // std::priority_queue is a max-heap by default; invert for min-heap by f.
                    return f > o.f;
                }
            };

            std::priority_queue<PQItem> open;
            std::unordered_map<std::uint64_t, float> gScore;
            std::unordered_map<std::uint64_t, std::pair<int, int>> parent;

            if (grid.width > 0 && grid.height > 0)
            {
                const std::size_t cap =
                    static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
                gScore.reserve(cap);
                parent.reserve(cap);
            }

            const auto h = [&](int x, int y) noexcept -> float
            {
                return heuristic_cost(x, y, gx, gy, allowDiagonal, costStraight, costDiagonal);
            };

            const auto startKey = Pack(sx, sy);
            gScore[startKey] = 0.0f;
            open.push(PQItem{ sx, sy, heuristicWeight * h(sx, sy), 0.0f });

            static constexpr int kDirs4[4][2] = {
                { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
            };
            static constexpr int kDirsDiag[4][2] = {
                { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 }
            };

            while (!open.empty())
            {
                const PQItem cur = open.top();
                open.pop();

                const auto curKey = Pack(cur.x, cur.y);
                const auto bestIt = gScore.find(curKey);
                if (bestIt != gScore.end() && cur.g > bestIt->second)
                    continue;

                if (cur.x == gx && cur.y == gy)
                    return Reconstruct(parent, sx, sy, gx, gy);

                auto relax = [&](int dx, int dy)
                {
                    if (!canStep(grid, cur.x, cur.y, dx, dy, allowDiagonal, dontCrossCorners))
                        return;

                    const int nx = cur.x + dx;
                    const int ny = cur.y + dy;
                    const auto nKey = Pack(nx, ny);

                    const float step = (dx != 0 && dy != 0) ? costDiagonal : costStraight;
                    const float newG = cur.g + step;

                    const auto it = gScore.find(nKey);
                    if (it == gScore.end() || newG < it->second)
                    {
                        gScore[nKey] = newG;
                        parent[nKey] = { cur.x, cur.y };

                        const float f = newG + heuristicWeight * h(nx, ny);
                        open.push(PQItem{ nx, ny, f, newG });
                    }
                };

                for (const auto& d : kDirs4)
                    relax(d[0], d[1]);

                if (allowDiagonal)
                {
                    for (const auto& d : kDirsDiag)
                        relax(d[0], d[1]);
                }
            }

            return {};
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
            }
            else if (dx != 0) // horizontal
            {
                if ((blocked(g, x, y + 1) && g.passable(x + dx, y + 1)) ||
                    (blocked(g, x, y - 1) && g.passable(x + dx, y - 1)))
                {
                    return std::make_pair(x, y);
                }
            }
            else // vertical (dy != 0)
            {
                if ((blocked(g, x + 1, y) && g.passable(x + 1, y + dy)) ||
                    (blocked(g, x - 1, y) && g.passable(x - 1, y + dy)))
                {
                    return std::make_pair(x, y);
                }
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

    // Cost/weight overload: THIS is what JpsOptions should drive.
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners,
                float costStraight,
                float costDiagonal,
                float heuristicWeight)
    {
        if (!grid.passable(sx, sy) || !grid.passable(gx, gy))
            return {};

        if (sx == gx && sy == gy)
            return { { sx, sy } };

        // Sanitize inputs (handles <=0 and NaN).
        costStraight   = sanitizePositive(costStraight, 1.0f);
        costDiagonal   = sanitizePositive(costDiagonal, 1.41421356237f * costStraight);
        heuristicWeight = (heuristicWeight > 0.0f) ? heuristicWeight : 0.0f;

        // If diagonal moves are disabled, or diagonals are dominated by two straights,
        // JPS8 pruning can miss paths. Use plain A* in those regimes.
        if (!allowDiagonal || costDiagonal > 2.0f * costStraight)
        {
            return FindPathAStarFallback(grid,
                                         sx, sy, gx, gy,
                                         allowDiagonal, dontCrossCorners,
                                         costStraight, costDiagonal, heuristicWeight);
        }

        struct PQItem
        {
            int x{}, y{};
            float f{}, g{};
            int px{}, py{}; // parent (for direction pruning)

            bool operator<(const PQItem& o) const noexcept
            {
                return f > o.f;
            }
        };

        std::priority_queue<PQItem> open;
        std::unordered_map<std::uint64_t, float> gScore;
        std::unordered_map<std::uint64_t, std::pair<int, int>> parent;

        if (grid.width > 0 && grid.height > 0)
        {
            const std::size_t cap =
                static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
            gScore.reserve(cap);
            parent.reserve(cap);
        }

        const auto h = [&](int x, int y) noexcept -> float
        {
            return heuristic_cost(x, y, gx, gy, allowDiagonal, costStraight, costDiagonal);
        };

        const auto startKey = Pack(sx, sy);
        gScore[startKey] = 0.0f;

        open.push(PQItem{ sx, sy, heuristicWeight * h(sx, sy), 0.0f, sx, sy });

        std::vector<std::pair<int, int>> dirs;
        dirs.reserve(8);

        while (!open.empty())
        {
            const PQItem cur = open.top();
            open.pop();

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

                // IMPORTANT: g-cost now uses caller-provided costs.
                const float newG = cur.g + segment_cost(cur.x, cur.y, jx, jy, costStraight, costDiagonal);

                const auto it = gScore.find(key);
                if (it == gScore.end() || newG < it->second)
                {
                    gScore[key] = newG;
                    parent[key] = { cur.x, cur.y };

                    // IMPORTANT: f-score now uses caller-provided heuristicWeight.
                    const float f = newG + heuristicWeight * h(jx, jy);
                    open.push(PQItem{ jx, jy, f, newG, cur.x, cur.y });
                }
            }
        }

        return {};
    }

    // Back-compat 7-arg overload (classic costs/weight).
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners)
    {
        return FindPathJPS(grid,
                           sx, sy, gx, gy,
                           allowDiagonal, dontCrossCorners,
                           /*costStraight=*/1.0f,
                           /*costDiagonal=*/1.41421356237f,
                           /*heuristicWeight=*/1.0f);
    }

} // namespace colony::path
