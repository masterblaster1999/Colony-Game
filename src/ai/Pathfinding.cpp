// src/ai/Pathfinding.cpp
// Windows-focused, self-contained TU (Unity-build friendly, resilient to angle-bracket stripping)

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

// Project header that declares GridView, Point, PFResult, etc.
#include "Pathfinding.hpp"

// Standard library headers — included with quotes so CI filters that remove `<...>`
// can't corrupt them. MSVC will still find the standard headers on the include path.
#include "vector"
#include "queue"
#include "algorithm"
#include "cstdint"
#include "cstddef"   // size_t
#include "cstdlib"   // std::abs(int)
#include "cmath"     // std::abs overloads
#include "climits"   // INT_MAX

// -----------------------------------------------------------------------------
// Template shielding: never write raw `<T>` in this file. These macros expand to
// real angle brackets only *after* preprocessing, so a naive text stripper can't
// damage the source.
// -----------------------------------------------------------------------------
#ifndef CG_LT_GT_DEFINED
#  define CG_LT_GT_DEFINED
#  define LT <
#  define GT >
#  define VEC(T)            std::vector LT T GT
#  define PRIQUEUE(T, C, L) std::priority_queue LT T, C, L GT
#endif

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Manhattan heuristic for a 4-connected grid.
// Use raw ints so we don't depend on `Point` being visible at this exact spot
// in Unity/Jumbo builds (prevents C4430/C2143 cascades around 'Point').
static inline int heuristic(int ax, int ay, int bx, int by) noexcept {
    return std::abs(ax - bx) + std::abs(ay - by);
}

// -----------------------------------------------------------------------------
// Primary A* Implementation (preferred API)
//   PFResult is assumed to be an *unscoped* enum with enumerators:
//   Found, NoPath, Aborted  (matches your error log where PFResult::Found failed).
// -----------------------------------------------------------------------------
PFResult aStar(const GridView& g, Point start, Point goal,
               VEC(Point)& out, int maxExpandedNodes)
{
    out.clear();

    // Basic validation (avoid depending on transitive includes)
    if (g.w <= 0 || g.h <= 0) return NoPath;

    // If your GridView exposes function pointers or std::function for walkable/cost,
    // this catches null; if they are methods, these lines still compile and are harmless.
    if (!g.walkable || !g.cost) return NoPath;

    const int W = g.w;
    const int H = g.h;

    auto inBounds = [&](int x, int y) noexcept -> bool {
        return (x >= 0 && y >= 0 && x LT W && y LT H);
    };

    if (!inBounds(start.x, start.y)) return NoPath;
    if (!inBounds(goal.x,  goal.y))  return NoPath;
    if (!g.walkable(start.x, start.y)) return NoPath;
    if (!g.walkable(goal.x,  goal.y))  return NoPath;

    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        return Found;
    }

    const int N = W * H;
    // Use function-call form to dodge Windows min/max macro collisions.
    const int INF = (std::min)(INT_MAX / 4, 0x3f3f3f3f);

    // Per-node state
    VEC(int)     gCost(N, INF);
    VEC(int)     parent(N, -1);
    VEC(uint8_t) closed(N, 0);
    VEC(int)     bestF(N, INF); // best known f-score for lazy PQ updates

    auto toIndex = [&](int x, int y) noexcept -> int { return y * W + x; };

    const int startIdx = toIndex(start.x, start.y);
    const int goalIdx  = toIndex(goal.x,  goal.y);

    // Min-heap node
    struct Node { int f; int tie; int idx; };

    // Lower f first; on ties, prefer lower g (straighter/shorter so far).
    struct Greater {
        bool operator()(const Node& a, const Node& b) const noexcept {
            if (a.f != b.f)   return a.f > b.f;
            return a.tie > b.tie;
        }
    };

    PRIQUEUE(Node, VEC(Node), Greater) open;

    // Seed
    gCost[static_cast<size_t>(startIdx)] = 0;
    {
        const int h0 = heuristic(start.x, start.y, goal.x, goal.y);
        bestF[static_cast<size_t>(startIdx)] = h0;
        open.push(Node{ h0, 0, startIdx });
    }

    int expanded = 0;

    while (!open.empty()) {
        // Lazy-pop: discard stale entries
        const Node top = open.top();
        open.pop();

        const int curF = top.f;
        const int cur  = top.idx;

        if (closed[static_cast<size_t>(cur)]) continue;
        if (curF > bestF[static_cast<size_t>(cur)]) continue;

        // Goal?
        if (cur == goalIdx) {
            // Reconstruct into `out` (forward order) without raw `<...>` usage.
            const int roughLen = (std::max)(0, heuristic(start.x, start.y, goal.x, goal.y) + 8);
            out.reserve(static_cast<size_t>((std::min)(roughLen, N)));

            // Collect reversed indices first to avoid a second VEC(Point).
            VEC(int) revIdx;
            revIdx.reserve(static_cast<size_t>((std::min)(roughLen, N)));
            for (int p = cur; p != -1; p = parent[static_cast<size_t>(p)]) {
                revIdx.push_back(p);
            }

            for (int i = static_cast<int>(revIdx.size()) - 1; i >= 0; --i) {
                const int p  = revIdx[static_cast<size_t>(i)];
                const int px = p % W;
                const int py = p / W;
                out.push_back(Point{ px, py });
            }
            return Found;
        }

        closed[static_cast<size_t>(cur)] = 1;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes)
            return Aborted;

        // Current cell
        const int cx = cur % W;
        const int cy = cur / W;

        // 4-neighborhood (no diagonals)
        const int nbrX[4] = { cx + 1, cx - 1, cx,     cx     };
        const int nbrY[4] = { cy,     cy,     cy + 1, cy - 1 };

        for (int k = 0; k < 4; ++k) {
            const int nx = nbrX[k], ny = nbrY[k];
            if (!inBounds(nx, ny))   continue;
            if (!g.walkable(nx, ny)) continue;

            const int nIdx = toIndex(nx, ny);
            if (closed[static_cast<size_t>(nIdx)]) continue;

            // Function-call form to dodge min/max macro collisions.
            const int stepCost  = (std::max)(1, g.cost(nx, ny));
            const int tentative = gCost[static_cast<size_t>(cur)] + stepCost;

            if (tentative < gCost[static_cast<size_t>(nIdx)]) {
                parent[static_cast<size_t>(nIdx)] = cur;
                gCost[static_cast<size_t>(nIdx)]  = tentative;

                const int f   = tentative + heuristic(nx, ny, goal.x, goal.y);
                const int tie = tentative; // prefer lower g on ties

                if (f < bestF[static_cast<size_t>(nIdx)]) {
                    bestF[static_cast<size_t>(nIdx)] = f;
                    open.push(Node{ f, tie, nIdx });
                }
            }
        }
    }

    return NoPath;
}

// -----------------------------------------------------------------------------
// Bridging overload: some call sites expect `aStar(g, s, t) -> std::vector<Point>`
// (Keep raw `<...>` out of the source via VEC macro.)
// -----------------------------------------------------------------------------
VEC(Point) aStar(const GridView& g, Point start, Point goal) {
    VEC(Point) path;
    (void)aStar(g, start, goal, path, -1); // -1 → unlimited expansions
    return path;
}

// If you want to avoid macro leakage outside this TU, you can uncomment:
// #undef PRIQUEUE
// #undef VEC
// #undef GT
// #undef LT
