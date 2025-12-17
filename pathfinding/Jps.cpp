// Jps.cpp — MSVC-friendly implementation of Jump Point Search
#include "JpsCore.hpp"

#include <algorithm>     // min, max, reverse
#include <cmath>         // sqrt
#include <cstddef>       // size_t
#include <cstdint>       // uint64_t
#include <cstdlib>       // abs, llabs
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

        static inline float metricCost(int x0, int y0,
                                       int x1, int y1,
                                       bool allowDiagonal,
                                       float costStraight,
                                       float costDiagonal) noexcept
        {
            const int dx = std::abs(x1 - x0);
            const int dy = std::abs(y1 - y0);

            if (!allowDiagonal)
                return costStraight * float(dx + dy);

            const int diag = std::min(dx, dy);
            const int str  = std::max(dx, dy) - diag;
            return costDiagonal * float(diag) + costStraight * float(str);
        }

        static inline float tieBreakCrossTerm(int x, int y,
                                              int sx, int sy,
                                              int gx, int gy,
                                              float costStraight) noexcept
        {
            // Cross-product tie-breaker (classic “prefer straight line” trick).
            // See e.g. Amit Patel’s heuristics page for the canonical formula. :contentReference[oaicite:3]{index=3}
            //
            // dx1 = current.x - goal.x
            // dy1 = current.y - goal.y
            // dx2 = start.x   - goal.x
            // dy2 = start.y   - goal.y
            // cross = abs(dx1*dy2 - dx2*dy1)
            //
            // We normalize by |start-goal| and multiply by a tiny epsilon so it behaves
            // like a tie-breaker and does not meaningfully distort f-scores.

            const long long dx1 = static_cast<long long>(x)  - gx;
            const long long dy1 = static_cast<long long>(y)  - gy;
            const long long dx2 = static_cast<long long>(sx) - gx;
            const long long dy2 = static_cast<long long>(sy) - gy;

            const long long cross = std::llabs(dx1 * dy2 - dx2 * dy1);

            const float lineLen = std::sqrt(float(dx2 * dx2 + dy2 * dy2));
            if (lineLen <= 1e-5f)
                return 0.0f;

            const float distFromLine = float(cross) / lineLen;

            // Tiny epsilon: behaves like a tie-break among near-equal f values.
            const float epsilon = 1e-6f * costStraight;
            return distFromLine * epsilon;
        }

        static std::vector<std::pair<int, int>>
        FindPathAStarFallback(const GridView& grid,
                              int sx, int sy,
                              int gx, int gy,
                              bool allowDiagonal,
                              bool dontCrossCorners,
                              float costStraight,
                              float costDiagonal,
                              float heuristicWeight,
                              bool tieBreakCross)
        {
            if (!grid.passable(sx, sy) || !grid.passable(gx, gy))
                return {};

            if (sx == gx && sy == gy)
                return { {sx, sy} };

            // Sanitize tuning
            if (costStraight <= 0.0f) costStraight = 1.0f;
            if (costDiagonal <= 0.0f) costDiagonal = 1.41421356237f;
            if (heuristicWeight < 0.0f) heuristicWeight = 0.0f;

            const bool useTie = tieBreakCross && heuristicWeight > 0.0f;

            struct PQItem
            {
                int x{}, y{};
                float f{}, g{};
                bool operator<(const PQItem& o) const noexcept
                {
                    // min-heap by f (priority_queue is max-heap by default)
                    return f > o.f;
                }
            };

            std::priority_queue<PQItem> open;
            std::unordered_map<std::uint64_t, float> gScore;
            std::unordered_map<std::uint64_t, std::pair<int, int>> parent;

            if (grid.width > 0 && grid.height > 0)
            {
                const std::size_t cap = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
                gScore.reserve(cap);
                parent.reserve(cap);
            }

            auto H = [&](int x, int y) noexcept -> float {
                return metricCost(x, y, gx, gy, allowDiagonal, costStraight, costDiagonal);
            };
            auto Tie = [&](int x, int y) noexcept -> float {
                return useTie ? tieBreakCrossTerm(x, y, sx, sy, gx, gy, costStraight) : 0.0f;
            };

            const auto startKey = Pack(sx, sy);
            gScore[startKey] = 0.0f;
            open.push(PQItem{ sx, sy, heuristicWeight * H(sx, sy) + Tie(sx, sy), 0.0f });

            static constexpr int DIRS4[4][2] = {
                { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
            };
            static constexpr int DIRS8[8][2] = {
                { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
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

                const auto* dirs = allowDiagonal ? DIRS8 : DIRS4;
                const int dirCount = allowDiagonal ? 8 : 4;

                for (int i = 0; i < dirCount; ++i)
                {
                    const int dx = dirs[i][0];
                    const int dy = dirs[i][1];

                    if (!canStep(grid, cur.x, cur.y, dx, dy, allowDiagonal, dontCrossCorners))
                        continue;

                    const int nx = cur.x + dx;
                    const int ny = cur.y + dy;

                    const float step = (dx != 0 && dy != 0) ? costDiagonal : costStraight;
                    const float newG = cur.g + step;

                    const auto key = Pack(nx, ny);
                    const auto it  = gScore.find(key);

                    if (it == gScore.end() || newG < it->second)
                    {
                        gScore[key] = newG;
                        parent[key] = { cur.x, cur.y };

                        const float f = newG + heuristicWeight * H(nx, ny) + Tie(nx, ny);
                        open.push(PQItem{ nx, ny, f, newG });
                    }
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

            // Forced-neighbor checks
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
            else // vertical
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

        if (dx != 0 && dy != 0) // diagonal move
        {
            // Natural neighbors
            pushDirIfFirstStepValid(dx, 0);
            pushDirIfFirstStepValid(0, dy);
            pushDirIfFirstStepValid(dx, dy);

            // Forced neighbors
            if (blocked(g, x - dx, y) && g.passable(x - dx, y + dy))
                pushDirIfFirstStepValid(-dx, dy);
            if (blocked(g, x, y - dy) && g.passable(x + dx, y - dy))
                pushDirIfFirstStepValid(dx, -dy);
        }
        else if (dx != 0) // horizontal
        {
            pushDirIfFirstStepValid(dx, 0);

            if (allowDiag)
            {
                if (blocked(g, x, y + 1) && g.passable(x + dx, y + 1))
                    pushDirIfFirstStepValid(dx, 1);
                if (blocked(g, x, y - 1) && g.passable(x + dx, y - 1))
                    pushDirIfFirstStepValid(dx, -1);
            }
        }
        else // vertical
        {
            pushDirIfFirstStepValid(0, dy);

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
                path.clear();
                return path;
            }

            x = it->second.first;
            y = it->second.second;
        }

        path.emplace_back(sx, sy);
        std::reverse(path.begin(), path.end());
        return path;
    }

    // -------------------------------------------------------------------------
    // Tuned overload (this is what JpsOptions should call)
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
                bool tieBreakCross)
    {
        if (!grid.passable(sx, sy) || !grid.passable(gx, gy))
            return {};

        if (sx == gx && sy == gy)
            return { {sx, sy} };

        // Sanitize tuning
        if (costStraight <= 0.0f) costStraight = 1.0f;
        if (costDiagonal <= 0.0f) costDiagonal = 1.41421356237f;
        if (heuristicWeight < 0.0f) heuristicWeight = 0.0f;

        // If diagonal edges are disabled (or never beneficial), JPS pruning assumptions break down.
        // Fallback to plain A* in those cases.
        if (!allowDiagonal || costDiagonal >= 2.0f * costStraight)
        {
            return FindPathAStarFallback(grid,
                                         sx, sy, gx, gy,
                                         /*allowDiagonal=*/false,
                                         dontCrossCorners,
                                         costStraight, costDiagonal,
                                         heuristicWeight,
                                         tieBreakCross);
        }

        const bool useTie = tieBreakCross && heuristicWeight > 0.0f;

        auto H = [&](int x, int y) noexcept -> float {
            return metricCost(x, y, gx, gy, /*allowDiagonal=*/true, costStraight, costDiagonal);
        };
        auto Tie = [&](int x, int y) noexcept -> float {
            return useTie ? tieBreakCrossTerm(x, y, sx, sy, gx, gy, costStraight) : 0.0f;
        };

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
            const std::size_t cap = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
            gScore.reserve(cap);
            parent.reserve(cap);
        }

        const auto startKey = Pack(sx, sy);
        gScore[startKey] = 0.0f;

        open.push(PQItem{ sx, sy, heuristicWeight * H(sx, sy) + Tie(sx, sy), 0.0f, sx, sy });

        std::vector<std::pair<int, int>> dirs;
        dirs.reserve(8);

        while (!open.empty())
        {
            const PQItem cur = open.top();
            open.pop();

            // Skip stale entries
            const auto curKey = Pack(cur.x, cur.y);
            const auto bestIt = gScore.find(curKey);
            if (bestIt != gScore.end() && cur.g > bestIt->second)
                continue;

            if (cur.x == gx && cur.y == gy)
                return Reconstruct(parent, sx, sy, gx, gy);

            PruneNeighbors(grid,
                           cur.x, cur.y,
                           sgn(cur.x - cur.px), sgn(cur.y - cur.py),
                           /*allowDiag=*/true, dontCrossCorners,
                           dirs);

            for (const auto& d : dirs)
            {
                const int dx = d.first;
                const int dy = d.second;

                auto jp = Jump(grid, cur.x, cur.y, dx, dy, gx, gy, /*allowDiag=*/true, dontCrossCorners);
                if (!jp)
                    continue;

                const int jx = jp->first;
                const int jy = jp->second;
                const auto key = Pack(jx, jy);

                const float newG = cur.g + metricCost(cur.x, cur.y, jx, jy, /*allowDiagonal=*/true, costStraight, costDiagonal);

                const auto it = gScore.find(key);
                if (it == gScore.end() || newG < it->second)
                {
                    gScore[key] = newG;
                    parent[key] = { cur.x, cur.y };

                    const float f = newG + heuristicWeight * H(jx, jy) + Tie(jx, jy);
                    open.push(PQItem{ jx, jy, f, newG, cur.x, cur.y });
                }
            }
        }

        return {};
    }

    // -------------------------------------------------------------------------
    // Base overload (kept for existing call sites)
    // -------------------------------------------------------------------------
    std::vector<std::pair<int, int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal,
                bool dontCrossCorners)
    {
        // Preserve old behavior (fixed costs, no tie-break cross).
        return FindPathJPS(grid,
                           sx, sy, gx, gy,
                           allowDiagonal, dontCrossCorners,
                           /*costStraight=*/1.0f,
                           /*costDiagonal=*/1.41421356237f,
                           /*heuristicWeight=*/1.0f,
                           /*tieBreakCross=*/false);
    }

    // NOTE:
    // Do NOT implement the 6-arg FindPathJPS overload here.
    // It is inline in JpsCore.hpp.

} // namespace colony::path
