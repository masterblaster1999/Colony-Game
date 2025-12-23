#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>   // std::abs
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "ai/PathTypes.hpp"

// -----------------------------------------------------------------------------
// Minimal, typed A* interface for grid-based gameplay systems.
// - Header-only (inline) so it can be dropped into small tools/demos.
// - 4-neighbour grid (N/S/E/W) with optional per-tile step cost.
// - Explicit result code + optional expansion budget for time-slicing.
//
// Notes:
// - This header intentionally defines a *global* Point/GridView for easy embedding.
// - A backward-compat namespace alias is provided at the bottom: cg::pf::...
// -----------------------------------------------------------------------------

// Keep Point simple and visible; several call sites use std::vector<Point>.
struct Point {
    int x{0};
    int y{0};
};

struct GridView {
    int w{0};
    int h{0};

    // Return whether tile (x,y) can be traversed.
    std::function<bool(int,int)> walkable;

    // Non-negative step cost for entering (x,y). Use >= 1 for "normal" tiles.
    std::function<int(int,int)> cost;

    [[nodiscard]] bool inBounds(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < w && y < h; }
    [[nodiscard]] constexpr int index(int x, int y) const noexcept { return y * w + x; }
    [[nodiscard]] Point fromIndex(int idx) const noexcept { return { idx % w, idx / w }; }
};

enum class PFResult {
    Found,
    NoPath,
    Aborted, // exceeded maxExpandedNodes
};

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
// A* on a 4-neighbour grid. Writes path to outPath (start..goal).
// If maxExpandedNodes >= 0, search aborts after exceeding that budget.
[[nodiscard]] inline PFResult aStar(const GridView& g,
                                   Point start,
                                   Point goal,
                                   std::vector<Point>& outPath,
                                   int maxExpandedNodes = -1)
{
    outPath.clear();

    if (g.w <= 0 || g.h <= 0)
        return PFResult::NoPath;

    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y))
        return PFResult::NoPath;

    // Graceful defaults if caller didn't provide callbacks.
    const auto walkable = g.walkable ? g.walkable : [](int, int) { return true; };
    const auto stepCost = g.cost ? g.cost : [](int, int) { return 1; };

    if (!walkable(start.x, start.y) || !walkable(goal.x, goal.y))
        return PFResult::NoPath;

    const int n = g.w * g.h;
    constexpr int kInf = (std::numeric_limits<int>::max)() / 4;

    std::vector<int> gScore(static_cast<std::size_t>(n), kInf);
    std::vector<int> parent(static_cast<std::size_t>(n), -1);
    std::vector<std::uint8_t> closed(static_cast<std::size_t>(n), 0);

    const auto heuristic = [](Point a, Point b) noexcept {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y); // Manhattan for 4-neighbour movement
    };

    struct Node {
        int idx;
        int f;
        int g;
    };
    struct NodeCmp {
        bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; } // min-heap
    };

    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    const int sIdx = g.index(start.x, start.y);
    const int gIdx = g.index(goal.x,  goal.y);

    gScore[static_cast<std::size_t>(sIdx)] = 0;
    open.push(Node{ sIdx, heuristic(start, goal), 0 });

    int expanded = 0;

    // N/S/E/W
    constexpr int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    while (!open.empty())
    {
        const Node cur = open.top();
        open.pop();

        if (closed[static_cast<std::size_t>(cur.idx)])
            continue;
        closed[static_cast<std::size_t>(cur.idx)] = 1;

        if (cur.idx == gIdx)
        {
            // Reconstruct path (goal -> start)
            int idx = gIdx;
            while (idx != -1)
            {
                outPath.push_back(g.fromIndex(idx));
                if (idx == sIdx)
                    break;
                idx = parent[static_cast<std::size_t>(idx)];
            }
            std::reverse(outPath.begin(), outPath.end());
            return PFResult::Found;
        }

        if (maxExpandedNodes >= 0 && expanded++ >= maxExpandedNodes)
        {
            outPath.clear();
            return PFResult::Aborted;
        }

        const Point cp = g.fromIndex(cur.idx);
        const int curG = gScore[static_cast<std::size_t>(cur.idx)];

        for (const auto& d : dirs)
        {
            const int nx = cp.x + d[0];
            const int ny = cp.y + d[1];

            if (!g.inBounds(nx, ny))
                continue;
            if (!walkable(nx, ny))
                continue;

            const int nIdx = g.index(nx, ny);
            if (closed[static_cast<std::size_t>(nIdx)])
                continue;

            int step = stepCost(nx, ny);
            if (step < 0) step = 1;

            // Avoid overflow if curG is already huge.
            if (curG > kInf - step)
                continue;

            const int tentative = curG + step;
            if (tentative < gScore[static_cast<std::size_t>(nIdx)])
            {
                parent[static_cast<std::size_t>(nIdx)] = cur.idx;
                gScore[static_cast<std::size_t>(nIdx)] = tentative;

                const int f = tentative + heuristic(Point{ nx, ny }, goal);
                open.push(Node{ nIdx, f, tentative });
            }
        }
    }

    return PFResult::NoPath;
}

// Convenience wrapper returning a path directly (empty => no path or aborted).
[[nodiscard]] inline std::vector<Point>
aStar(const GridView& g, Point start, Point goal, int maxExpandedNodes = -1)
{
    std::vector<Point> p;
    const PFResult r = aStar(g, start, goal, p, maxExpandedNodes);
    if (r == PFResult::Found) return p;
    return {};
}

// -----------------------------------------------------------------------------
// Backward-compat aliases: if other files used cg::pf::{Point,Result,aStar}
// -----------------------------------------------------------------------------
namespace cg { namespace pf {
    using ::Point;
    using ::GridView;
    using ::PFResult;
    using Result = ::PFResult; // legacy name, if code used cg::pf::Result
    using ::aStar;             // brings both overloads into cg::pf::
}} // namespace cg::pf
