// src/ai/Pathfinding.cpp
#include "Pathfinding.hpp"
#include <queue>
#include <utility>

namespace cg::pf {

static inline int manhattan(Point a, Point b) noexcept {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

Result aStar(const GridView& g, Point start, Point goal,
             std::vector<Point>& out, int maxExpandedNodes)
{
    out.clear();

    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) {
        return Result::NoPath;
    }
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) {
        return Result::NoPath;
    }
    const int N = g.w * g.h;
    if (N <= 0) return Result::NoPath;

    // Costs/parents; use int for simplicity (tile costs small).
    constexpr int INF = std::numeric_limits<int>::max();
    std::vector<int> gCost(N, INF), fCost(N, INF), parent(N, -1);
    std::vector<uint8_t> closed(N, 0);

    auto h = [&](int x, int y) { return manhattan({x,y}, goal); };

    struct Node { int idx; int f; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; } };

    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    open.push({ g.index(start.x, start.y), h(start.x, start.y) });
    gCost[g.index(start.x, start.y)] = 0;
    fCost[g.index(start.x, start.y)] = h(start.x, start.y);

    int expanded = 0;
    while (!open.empty()) {
        const int cur = open.top().idx;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes) {
            return Result::Aborted; // caller may retry next tick
        }

        if (cur == g.index(goal.x, goal.y)) {
            // Reconstruct path
            std::vector<Point> rev;
            for (int p = cur; p != -1; p = parent[p]) rev.push_back(g.fromIndex(p));
            out.assign(rev.rbegin(), rev.rend());
            return Result::Found;
        }

        const Point p = g.fromIndex(cur);
        const int nbrX[4] = { p.x+1, p.x-1, p.x,   p.x   };
        const int nbrY[4] = { p.y,   p.y,   p.y+1, p.y-1 };

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
