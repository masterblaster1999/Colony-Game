// src/ai/Pathfinding.cpp
// Self-include first to guarantee types (e.g., Point, GridView, Result) are visible in Unity builds.
#include "Pathfinding.hpp"

#include <vector>
#include <queue>
#include <limits>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

namespace cg::pf {

// Keep Manhattan for 4-way movement (admissible & consistent for axis-aligned grids).
static inline int manhattan(const Point& a, const Point& b) noexcept {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    return dx + dy;
}

Result aStar(const GridView& g, Point start, Point goal,
             std::vector<Point>& out, int maxExpandedNodes)
{
    out.clear();

    // Basic validation
    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) {
        return Result::NoPath;
    }
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) {
        return Result::NoPath;
    }

    const int N = g.w * g.h;
    if (N <= 0) return Result::NoPath;

    const int startIdx = g.index(start.x, start.y);
    const int goalIdx  = g.index(goal.x, goal.y);

    // Costs/parents
    constexpr int INF = std::numeric_limits<int>::max();
    std::vector<int> gCost(N, INF), fCost(N, INF), parent(N, -1);
    std::vector<uint8_t> closed(N, 0);

    auto h = [&](int x, int y) noexcept { return manhattan(Point{x, y}, goal); };

    struct Node { int idx; int f; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; } };

    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    gCost[startIdx] = 0;
    fCost[startIdx] = h(start.x, start.y);
    open.push({ startIdx, fCost[startIdx] });

    int expanded = 0;

    while (!open.empty()) {
        const int cur = open.top().idx;
        open.pop();

        if (closed[cur]) continue;
        closed[cur] = 1;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes) {
            return Result::Aborted; // caller may retry next tick
        }

        if (cur == goalIdx) {
            // Reconstruct path
            std::vector<Point> rev;
            rev.reserve(64); // small default to reduce reallocs
            for (int p = cur; p != -1; p = parent[p]) {
                rev.push_back(g.fromIndex(p));
            }
            out.assign(rev.rbegin(), rev.rend());
            return Result::Found;
        }

        // If somehow current node had INF cost (shouldn't happen), skip relaxing neighbors.
        if (gCost[cur] == INF) continue;

        const Point p = g.fromIndex(cur);

        // 4-way neighbors (right, left, down, up)
        const int nbrX[4] = { p.x + 1, p.x - 1, p.x,     p.x     };
        const int nbrY[4] = { p.y,     p.y,     p.y + 1, p.y - 1 };

        for (int i = 0; i < 4; ++i) {
            const int nx = nbrX[i], ny = nbrY[i];
            if (!g.inBounds(nx, ny) || !g.walkable(nx, ny)) continue;

            const int nIdx = g.index(nx, ny);
            if (closed[nIdx]) continue;

            const int step = std::max(1, g.cost(nx, ny));
            const int tentative = gCost[cur] + step;
            if (tentative < gCost[nIdx]) {
                parent[nIdx] = cur;
                gCost[nIdx]  = tentative;
                const int f  = tentative + h(nx, ny);
                fCost[nIdx]  = f;
                open.push({ nIdx, f });
            }
        }
    }

    return Result::NoPath;
}

} // namespace cg::pf
