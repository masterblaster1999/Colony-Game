// Jps.cpp — MSVC-friendly implementation of Jump Point Search
#include "JpsCore.hpp"

#include <cassert>

namespace colony::path
{
    static inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

    float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept
    {
        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);
        if (!allowDiagonal) // Manhattan
            return float(dx + dy);
        // Octile distance: cost(diagonal)=sqrt(2), straight=1
        const float D  = 1.0f;
        const float D2 = 1.41421356237f;
        return D * float(dx + dy) + (D2 - 2.0f * D) * float(std::min(dx, dy));
    }

    // Check for any blocked cell among 8 neighbors used in diagonal tests
    static inline bool blocked(const GridView& g, int x, int y) noexcept
    {
        return !g.inBounds(x, y) || g.isBlocked(x, y);
    }

    // Forced neighbor checks based on Harabor & Grastien (AAAI'11)
    // If moving horizontally (dx,dy)=(±1,0): a forced neighbor exists if
    // the cell above/below is blocked and the diagonal ahead in that side is free.
    // Similar tests for vertical and diagonal moves. See paper for details.
    // (This mirrors common reference implementations.)  :contentReference[oaicite:2]{index=2}
    std::optional<std::pair<int,int>>
    Jump(const GridView& g, int x, int y, int dx, int dy,
         int goalX, int goalY, bool allowDiag)
    {
        // Step once
        x += dx; y += dy;

        if (!g.passable(x, y))
            return std::nullopt;
        if (x == goalX && y == goalY)
            return std::make_pair(x, y);

        if (dx != 0 && dy != 0) // diagonal move
        {
            // Forced neighbors for diagonal
            if ( (blocked(g, x - dx, y + dy) && g.passable(x, y + dy)) ||
                 (blocked(g, x + dx, y - dy) && g.passable(x + dx, y)) )
            {
                return std::make_pair(x, y);
            }

            // When moving diagonally, we also need to check for jump points
            // in each cardinal direction.
            if (Jump(g, x, y, dx, 0, goalX, goalY, allowDiag).has_value() ||
                Jump(g, x, y, 0, dy, goalX, goalY, allowDiag).has_value())
            {
                return std::make_pair(x, y);
            }
        }
        else
        {
            if (dx != 0) // horizontal
            {
                if ( (blocked(g, x, y + 1) && g.passable(x + dx, y + 1)) ||
                     (blocked(g, x, y - 1) && g.passable(x + dx, y - 1)) )
                {
                    return std::make_pair(x, y);
                }
            }
            else // vertical
            {
                if ( (blocked(g, x + 1, y) && g.passable(x + 1, y + dy)) ||
                     (blocked(g, x - 1, y) && g.passable(x - 1, y + dy)) )
                {
                    return std::make_pair(x, y);
                }
            }
        }

        // If diagonal moves are disallowed, stop extending diagonal directions
        if (!allowDiag && dx != 0 && dy != 0)
            return std::nullopt;

        // If we can continue in this direction (for diagonals, require the
        // side steps to be open to avoid cutting corners), keep jumping.
        if (dx != 0 && dy != 0)
        {
            if (g.passable(x + dx, y) && g.passable(x, y + dy))
                return Jump(g, x, y, dx, dy, goalX, goalY, allowDiag);
            return std::nullopt;
        }
        else
        {
            return Jump(g, x, y, dx, dy, goalX, goalY, allowDiag);
        }
    }

    void PruneNeighbors(const GridView& g, int x, int y,
                        int dx, int dy, bool allowDiag,
                        std::vector<std::pair<int,int>>& outDirs)
    {
        outDirs.clear();

        if (dx == 0 && dy == 0)
        {
            // Start node: return all natural neighbors
            static const int DIR8[8][2] = {
                { 1, 0},{-1, 0},{ 0, 1},{ 0,-1},
                { 1, 1},{-1, 1},{ 1,-1},{-1,-1}
            };
            static const int DIR4[4][2] = {
                { 1, 0},{-1, 0},{ 0, 1},{ 0,-1}
            };
            if (allowDiag)
            {
                for (auto& d : DIR8) outDirs.emplace_back(d[0], d[1]);
            }
            else
            {
                for (auto& d : DIR4) outDirs.emplace_back(d[0], d[1]);
            }
            return;
        }

        // Moving direction (dx,dy) from parent -> current
        if (dx != 0 && dy != 0) // diagonal move
        {
            // Natural neighbors
            if (g.passable(x + dx, y)) outDirs.emplace_back(dx, 0);
            if (g.passable(x, y + dy)) outDirs.emplace_back(0, dy);
            if (allowDiag && g.passable(x + dx, y) && g.passable(x, y + dy))
                outDirs.emplace_back(dx, dy);

            // Forced neighbors when cutting near corners
            if (!g.passable(x - dx, y) && g.passable(x - dx, y + dy))
                outDirs.emplace_back(-dx, dy);
            if (!g.passable(x, y - dy) && g.passable(x + dx, y - dy))
                outDirs.emplace_back(dx, -dy);
        }
        else if (dx != 0) // horizontal
        {
            if (g.passable(x + dx, y)) outDirs.emplace_back(dx, 0);

            if (allowDiag)
            {
                if (!g.passable(x, y + 1) && g.passable(x + dx, y + 1))
                    outDirs.emplace_back(dx, 1);
                if (!g.passable(x, y - 1) && g.passable(x + dx, y - 1))
                    outDirs.emplace_back(dx, -1);
            }
        }
        else // vertical
        {
            if (g.passable(x, y + dy)) outDirs.emplace_back(0, dy);

            if (allowDiag)
            {
                if (!g.passable(x + 1, y) && g.passable(x + 1, y + dy))
                    outDirs.emplace_back(1, dy);
                if (!g.passable(x - 1, y) && g.passable(x - 1, y + dy))
                    outDirs.emplace_back(-1, dy);
            }
        }
    }

    std::vector<std::pair<int,int>>
    Reconstruct(const std::unordered_map<std::uint64_t, std::pair<int,int>>& parent,
                int sx, int sy, int gx, int gy)
    {
        std::vector<std::pair<int,int>> path;
        int x = gx, y = gy;
        while (!(x == sx && y == sy))
        {
            path.emplace_back(x, y);
            auto it = parent.find(Pack(x, y));
            if (it == parent.end()) {
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

    std::vector<std::pair<int,int>>
    FindPathJPS(const GridView& grid,
                int sx, int sy,
                int gx, int gy,
                bool allowDiagonal)
    {
        struct PQItem
        {
            int x, y; float f, g; int px, py;
            bool operator<(const PQItem& o) const { return f > o.f; } // min-heap
        };

        std::priority_queue<PQItem> open;
        std::unordered_map<std::uint64_t, float> gScore;
        std::unordered_map<std::uint64_t, std::pair<int,int>> parent;

        auto startKey = Pack(sx, sy);
        gScore[startKey] = 0.0f;
        open.push({sx, sy, Octile(sx, sy, gx, gy, allowDiagonal), 0.0f, sx, sy});

        std::vector<std::pair<int,int>> dirs;
        while (!open.empty())
        {
            PQItem cur = open.top(); open.pop();
            if (cur.x == gx && cur.y == gy)
                return Reconstruct(parent, sx, sy, gx, gy);

            PruneNeighbors(grid, cur.x, cur.y,
                           sgn(cur.x - cur.px), sgn(cur.y - cur.py),
                           allowDiagonal, dirs);

            for (auto [dx, dy] : dirs)
            {
                auto jp = Jump(grid, cur.x, cur.y, dx, dy, gx, gy, allowDiagonal);
                if (!jp) continue;

                int jx = jp->first, jy = jp->second;
                auto key = Pack(jx, jy);

                float newG = cur.g + Octile(cur.x, cur.y, jx, jy, allowDiagonal);
                auto it = gScore.find(key);
                if (it == gScore.end() || newG < it->second)
                {
                    gScore[key] = newG;
                    parent[key] = {cur.x, cur.y};
                    float f = newG + Octile(jx, jy, gx, gy, allowDiagonal);
                    open.push({jx, jy, f, newG, cur.x, cur.y});
                }
            }
        }
        return {}; // no path
    }
} // namespace colony::path
