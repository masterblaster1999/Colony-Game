// Pathfinding.cpp — Windows-focused, self-contained TU for MSVC (Unity-build friendly)

#ifdef _WIN32
// Keep this TU robust in MSVC Unity builds on Windows (min/max macros, etc.)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

// Suppress two benign warnings originating from Pathfinding.hpp in MSVC builds.
// (If you prefer, fix them in the header by marking 'dmax' and 'emptyOpen' as [[maybe_unused]].)
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4189) // local variable initialized but not referenced
#  pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include "Pathfinding.hpp"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

// ---- Bring types into scope regardless of whether they live in :: or ::ai ----
// If ai is already defined in the header, this just re-opens it (harmless).
namespace ai {}
// Make Point, GridView, PFResult, and any unscoped enumerators like Found/NoPath visible
using namespace ai;

// STL includes used in the definitions below.
#include <vector>
#include <cmath>
#include <limits>
#include <cstdint>
#include <algorithm>
#include <cstddef> // size_t
#include <cstdlib> // std::abs(int)

#include "IndexedPriorityQueue.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Manhattan heuristic that matches 4-connected grids.
// Kept as an int for fast integer math inside the main loop.
static inline int heuristic(const Point& a, const Point& b) noexcept {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

// -----------------------------------------------------------------------------
// Bridging overload (classic return type, no trailing-return syntax).
// Some older call sites (per your CI logs) expect `aStar(g, s, t)` returning
// `std::vector<Point>`.  We forward to the primary PFResult-based API.
// -----------------------------------------------------------------------------
std::vector<Point> aStar(const GridView& g, Point start, Point goal) {
    std::vector<Point> path;
    const int defaultLimit = -1;  // unlimited expansion
    const PFResult res = aStar(g, start, goal, path, defaultLimit);
    if (res == Found) {
        return path;
    }
    return {}; // NoPath or Aborted -> empty vector
}

// -----------------------------------------------------------------------------
// Primary A* Implementation (preferred API)
// -----------------------------------------------------------------------------
PFResult aStar(const GridView& g, Point start, Point goal,
               std::vector<Point>& out, int maxExpandedNodes)
{
    out.clear();

    // Basic validation
    if (g.w <= 0 || g.h <= 0) return NoPath;
    if (!g.walkable || !g.cost) return NoPath;
    if (!g.inBounds(start.x, start.y)) return NoPath;
    if (!g.inBounds(goal.x, goal.y)) return NoPath;
    if (!g.walkable(start.x, start.y)) return NoPath;
    if (!g.walkable(goal.x, goal.y)) return NoPath;
    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        return Found;
    }

    const int N = g.w * g.h;
    // Function-style call avoids min/max macro interference from <Windows.h>.
    constexpr int INF = (std::numeric_limits<int>::max)();

    std::vector<int>     gCost(N, INF);
    std::vector<int>     parent(N, -1);
    std::vector<uint8_t> closed(N, 0);

    const int startIdx = g.index(start.x, start.y);
    const int goalIdx  = g.index(goal.x, goal.y);

    const auto h = [&](int x, int y) noexcept {
        return heuristic(Point{ x, y }, goal);
    };

    // --- Use an indexed min-heap that supports decrease-key ---
    IndexedPriorityQueue open(static_cast<std::size_t>(N));

    // Push start with a tiny tie-break on g to prefer straight-ish continuations
    gCost[startIdx] = 0;
    {
        const float key = static_cast<float>(h(start.x, start.y))
                        + 1e-4f * static_cast<float>(gCost[startIdx]);
        open.push_or_decrease(startIdx, key);
    }

    int expanded = 0;

    while (!open.empty()) {
        const int cur = open.pop_min();
        if (closed[cur]) continue;

        // If we just popped the goal, we're done. Check this BEFORE counting aborts.
        if (cur == goalIdx) {
            // Reconstruct path
            std::vector<Point> rev;

            // Reserve a rough, safe lower bound to reduce reallocs.
            // Path length is at least Manhattan distance; obstacles can increase it,
            // but reserve() is only a hint.
            const int roughLen = (std::max)(0, heuristic(start, goal) + 8);
            rev.reserve(static_cast<std::size_t>((std::min)(roughLen, N)));

            for (int p = cur; p != -1; p = parent[p]) {
                rev.push_back(g.fromIndex(p));
            }
            out.assign(rev.rbegin(), rev.rend());
            return Found;
        }

        closed[cur] = 1;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes)
            return Aborted;

        const Point p = g.fromIndex(cur);
        const int nbrX[4] = { p.x + 1, p.x - 1, p.x,     p.x     };
        const int nbrY[4] = { p.y,     p.y,     p.y + 1, p.y - 1 };

        for (int i = 0; i < 4; ++i) {
            const int nx = nbrX[i], ny = nbrY[i];
            if (!g.inBounds(nx, ny) || !g.walkable(nx, ny)) continue;

            const int nIdx = g.index(nx, ny);
            if (nIdx < 0 || nIdx >= N) continue; // defensive, in case index() changes
            if (closed[nIdx]) continue;

            // Use function-call form to dodge min/max macro collisions.
            const int stepCost  = (std::max)(1, g.cost(nx, ny));
            const int tentative = gCost[cur] + stepCost;

            if (tentative < gCost[nIdx]) {
                parent[nIdx] = cur;
                gCost[nIdx]  = tentative;

                const int f     = tentative + h(nx, ny);
                const float key = static_cast<float>(f)
                                + 1e-4f * static_cast<float>(tentative);
                open.push_or_decrease(nIdx, key);
            }
        }
    }

    return NoPath;
}
