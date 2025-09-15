// Pathfinding.cpp
#include "Pathfinding.hpp"

// Keep TU local safe against Windows pragma bleed in unity builds.
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#include <queue>
#include <algorithm>
#include <utility>
#include <limits>

namespace cg::pf {

// Manhattan heuristic (grid, 4-way)
static inline constexpr int manhattan(const Point& a, const Point& b) noexcept {
    const int dx = (a.x >= b.x) ? (a.x - b.x) : (b.x - a.x);
    const int dy = (a.y >= b.y) ? (a.y - b.y) : (b.y - a.y);
    return dx + dy;
}

Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::vector<Point>& out,
             const int maxExpandedNodes)
{
    out.clear();

    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) return Result::NoPath;
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) return Result::NoPath;

    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        return Result::Found;
    }

    const int N = g.w * g.h;
    if (N <= 0) return Result::NoPath;

    // Use function-style call to dodge potential macro collisions (MSVC + Windows headers).
    const int INF = (std::numeric_limits<int>::max)();

    std::vector<int> gCost(N, INF);
    std::vector<int> fCost(N, INF);
    std::vector<int> parent(N, -1);
    std::vector<unsigned char> closed(N, 0);

    const int startIdx = g.index(start.x, start.y);
    const int goalIdx  = g.index(goal.x,  goal.y);

    auto h = [&](int x, int y) -> int { return manhattan({x, y}, goal); };

    struct Node { int idx; int f; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; } };

    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    open.push({ startIdx, h(start.x, start.y) });

    gCost[startIdx] = 0;
    fCost[startIdx] = h(start.x, start.y);

    int expanded = 0;

    while (!open.empty()) {
        const Node node = open.top();
        open.pop();

        const int cur = node.idx;
        if (closed[cur]) continue;
        closed[cur] = 1;

        // Optional per-tick budget (lets the sim stay responsive under load)
        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes)
            return Result::Aborted;

        if (cur == goalIdx) {
            // Reconstruct path
            std::vector<Point> rev;
            for (int p = cur; p != -1; p = parent[p]) {
                rev.push_back(g.fromIndex(p));
            }
            out.assign(rev.rbegin(), rev.rend());
            return Result::Found;
        }

        const Point p = g.fromIndex(cur);
        const int nbrX[4] = { p.x + 1, p.x - 1, p.x,     p.x     };
        const int nbrY[4] = { p.y,     p.y,     p.y + 1, p.y - 1 };

        for (int i = 0; i < 4; ++i) {
            const int nx = nbrX[i], ny = nbrY[i];
            if (!g.inBounds(nx, ny) || !g.walkable(nx, ny)) continue;

            const int nIdx = g.index(nx, ny);
            if (closed[nIdx]) continue;

            // Ensure positive progress; avoid macro issues by using the 2-arg overload style.
            const int step     = (std::max)(1, g.cost(nx, ny));
            const int tentative = gCost[cur] + step;

            if (tentative < gCost[nIdx]) {
                parent[nIdx] = cur;
                gCost[nIdx]  = tentative;

                const int f = tentative + h(nx, ny);
                fCost[nIdx] = f;

                open.push({ nIdx, f });
            }
        }
    }

    return Result::NoPath;
}

} // namespace cg::pf
