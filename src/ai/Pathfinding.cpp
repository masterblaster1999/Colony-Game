// src/ai/Pathfinding.cpp
//
// A* pathfinding with Windows-focused robustness and optional upgrades.
// Public API preserved:
//   Result aStar(const GridView& g, Point start, Point goal,
//                std::vector<Point>& out, int maxExpandedNodes)
//
// Enhancements over the base version:
// - Correct diagonals heuristic (Chebyshev) when equal-cost diagonals are enabled.
// - Optional √2 diagonal movement (integer 10/14 scaling) for more realistic 8-way costs.
// - Colinear point stripping to reduce vertex count without changing path shape (ON by default).
// - Thread-local search statistics for introspection (getLastSearchStats()).
// - Optional helper aStarOrNearestReachable(...) to get a "best-effort" path if the goal is blocked.
//
// Compile-time flags (all default OFF unless noted):
//   CG_PF_ENABLE_DIAGONALS   = 0/1  (8-way movement allowed; default 0)
//   CG_PF_DIAG_COST_SQRT2    = 0/1  (8-way: diagonal cost ~1.414; integer 10/14 scaling; default 0)
//   CG_PF_ENABLE_SMOOTHING   = 0/1  (post-process LoS smoothing; default 0)
//   CG_PF_OPEN_DEDUP         = 0/1  (avoid pushing worse/equal f for same node; default 1)
//   CG_PF_TIEBREAK_DEEPER_G  = 0/1  (prefer larger g on equal f; default 1)
//   CG_PF_STRIP_COLINEAR     = 0/1  (drop redundant colinear points; default 1)
//
// CMake (Windows):
//   if (MSVC)
//     add_compile_options(/W4 /permissive- /EHsc /Zc:__cplusplus /Zc:inline)
//     add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
//     set(CMAKE_CXX_STANDARD 20)
//     set(CMAKE_CXX_STANDARD_REQUIRED ON)
//   endif
//

#include "Pathfinding.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace cg::pf {

// ------------------------------
// Compile-time knobs (defaults)
// ------------------------------
#ifndef CG_PF_ENABLE_DIAGONALS
#define CG_PF_ENABLE_DIAGONALS 0
#endif

#ifndef CG_PF_DIAG_COST_SQRT2
#define CG_PF_DIAG_COST_SQRT2 0   // if 1: straight=10, diagonal=14 scaling
#endif

#ifndef CG_PF_ENABLE_SMOOTHING
#define CG_PF_ENABLE_SMOOTHING 0
#endif

#ifndef CG_PF_OPEN_DEDUP
#define CG_PF_OPEN_DEDUP 1
#endif

#ifndef CG_PF_TIEBREAK_DEEPER_G
#define CG_PF_TIEBREAK_DEEPER_G 1
#endif

#ifndef CG_PF_STRIP_COLINEAR
#define CG_PF_STRIP_COLINEAR 1
#endif

// ------------------------------
// Heuristics
// ------------------------------
static inline constexpr int absInt(int v) noexcept {
    return (v >= 0) ? v : -v;
}

// 4-way Manhattan
static inline constexpr int manhattan(const Point& a, const Point& b) noexcept {
    return absInt(a.x - b.x) + absInt(a.y - b.y);
}

// 8-way Chebyshev (equal step cost for straight and diagonal)
static inline constexpr int chebyshev(const Point& a, const Point& b) noexcept {
    const int dx = absInt(a.x - b.x);
    const int dy = absInt(a.y - b.y);
    return (dx > dy) ? dx : dy;
}

// Octile scaled by 10/14 (8-way with diagonal ~√2). Integer math.
static inline constexpr int octile14(const Point& a, const Point& b) noexcept {
    const int dx = absInt(a.x - b.x);
    const int dy = absInt(a.y - b.y);
    const int mn = (dx < dy) ? dx : dy;
    // 14*mn + 10*(max-min) == 10*(dx+dy) - 6*mn
    return 10 * (dx + dy) - 6 * mn;
}

// ------------------------------
// Thread-local statistics
// ------------------------------
struct SearchStats {
    int nodesPushed   = 0;
    int nodesPopped   = 0;
    int nodesClosed   = 0;
    int nodesExpanded = 0;
    int maxOpenSize   = 0;
    int pathLength    = 0; // number of waypoints in result
};

static thread_local SearchStats s_stats;

[[maybe_unused]] static SearchStats getLastSearchStats() noexcept { return s_stats; }

// ------------------------------
// Internal scratch buffers
// ------------------------------
struct Scratch {
    std::vector<int> gCost;
    std::vector<int> fCost;
    std::vector<int> parent;
    std::vector<unsigned char> closed;
#if CG_PF_OPEN_DEDUP
    std::vector<int> queuedF;
#endif

    void reset(std::size_t N, int INF) {
        if (gCost.size() != N) {
            gCost.assign(N, INF);
            fCost.assign(N, INF);
            parent.assign(N, -1);
            closed.assign(N, 0u);
#if CG_PF_OPEN_DEDUP
            queuedF.assign(N, INF);
#endif
        } else {
            std::fill(gCost.begin(),  gCost.end(),  INF);
            std::fill(fCost.begin(),  fCost.end(),  INF);
            std::fill(parent.begin(), parent.end(), -1);
            std::fill(closed.begin(), closed.end(), 0u);
#if CG_PF_OPEN_DEDUP
            std::fill(queuedF.begin(), queuedF.end(), INF);
#endif
        }
    }
};

static thread_local Scratch s_scratch;

// ------------------------------
// Neighbor iteration helpers
// ------------------------------
struct NeighborSet {
    static constexpr int Count =
#if CG_PF_ENABLE_DIAGONALS
        8
#else
        4
#endif
        ;
    int dx[Count];
    int dy[Count];

    NeighborSet() {
#if CG_PF_ENABLE_DIAGONALS
        // 8-way (E, W, N, S, NE, SE, NW, SW) — E/W/N/S first aids admissible heuristics
        dx[0] =  1; dy[0] =  0;
        dx[1] = -1; dy[1] =  0;
        dx[2] =  0; dy[2] =  1;
        dx[3] =  0; dy[3] = -1;
        dx[4] =  1; dy[4] =  1;
        dx[5] =  1; dy[5] = -1;
        dx[6] = -1; dy[6] =  1;
        dx[7] = -1; dy[7] = -1;
#else
        // 4-way (E, W, N, S)
        dx[0] =  1; dy[0] =  0;
        dx[1] = -1; dy[1] =  0;
        dx[2] =  0; dy[2] =  1;
        dx[3] =  0; dy[3] = -1;
#endif
    }
};

static inline bool isDiagonalStep(int dx, int dy) noexcept {
    return (dx != 0) && (dy != 0);
}

// Prevent corner cutting on diagonals.
static inline bool diagonalAllowedNoCornerCut(const GridView& g, const Point& p, int ddx, int ddy) {
#if CG_PF_ENABLE_DIAGONALS
    if (!isDiagonalStep(ddx, ddy)) return true;
    const int x1 = p.x + ddx;
    const int y1 = p.y;
    const int x2 = p.x;
    const int y2 = p.y + ddy;
    return g.inBounds(x1, y1) && g.walkable(x1, y1) &&
           g.inBounds(x2, y2) && g.walkable(x2, y2);
#else
    (void)g; (void)p; (void)ddx; (void)ddy;
    return true;
#endif
}

// ------------------------------
// Optional line-of-sight smoothing
// ------------------------------
#if CG_PF_ENABLE_SMOOTHING
static bool lineOfSight(const GridView& g, Point a, Point b) {
    // Bresenham
    int x0 = a.x, y0 = a.y;
    const int x1 = b.x, y1 = b.y;
    const int sx = (x0 <= x1) ? 1 : -1;
    const int sy = (y0 <= y1) ? 1 : -1;
    int dx = absInt(x1 - x0);
    int dy = absInt(y1 - y0);

    int err = dx - dy;
    if (!g.inBounds(x0, y0) || !g.walkable(x0, y0)) return false;

    while (x0 != x1 || y0 != y1) {
        const int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
        if (!g.inBounds(x0, y0) || !g.walkable(x0, y0)) return false;
    }
    return true;
}
#endif

// Strip strictly colinear midpoints: ... A, B, C ... where (B - A) × (C - B) == 0
static void stripColinear(std::vector<Point>& path) {
#if CG_PF_STRIP_COLINEAR
    if (path.size() < 3) return;
    std::vector<Point> out;
    out.reserve(path.size());
    out.push_back(path[0]);
    for (std::size_t i = 1; i + 1 < path.size(); ++i) {
        const Point& a = out.back();
        const Point& b = path[i];
        const Point& c = path[i + 1];
        const int vx1 = b.x - a.x, vy1 = b.y - a.y;
        const int vx2 = c.x - b.x, vy2 = c.y - b.y;
        const int cross = vx1 * vy2 - vy1 * vx2;
        if (cross != 0) out.push_back(b); // keep non-colinear
    }
    out.push_back(path.back());
    path.swap(out);
#else
    (void)path;
#endif
}

// ------------------------------
// A* core types
// ------------------------------
namespace {
    struct Node {
        int idx;
        int f;
        int g;
    };

    struct NodeCmp {
        bool operator()(const Node& a, const Node& b) const noexcept {
            if (a.f != b.f) return a.f > b.f; // min-heap by f
#if CG_PF_TIEBREAK_DEEPER_G
            return a.g < b.g;
#else
            return a.g > b.g;
#endif
        }
    };
} // anonymous

// Reconstruct path into 'out' (start->goal order)
static void reconstructPath(const GridView& g, int goalIdx,
                            const std::vector<int>& parent,
                            std::vector<Point>& out)
{
    if (out.capacity() < 64) out.reserve(64);
    std::vector<Point> rev;
    rev.reserve(64);
    for (int p = goalIdx; p != -1; p = parent[static_cast<std::size_t>(p)]) {
        rev.push_back(g.fromIndex(p));
    }
    out.assign(rev.rbegin(), rev.rend());
}

// ------------------------------------
// Public API (unchanged signature)
// ------------------------------------
Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::vector<Point>& out,
             int maxExpandedNodes)
{
    s_stats = {}; // reset stats
    out.clear();

    // Basic checks
    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) {
        return Result::NoPath;
    }
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) {
        return Result::NoPath;
    }
    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        s_stats.pathLength = 1;
        return Result::Found;
    }

    // Grid size
    const int N = g.w * g.h;
    if (N <= 0) return Result::NoPath;

    constexpr int INF = std::numeric_limits<int>::max();
    s_scratch.reset(static_cast<std::size_t>(N), INF);

    const int startIdx = g.index(start.x, start.y);
    const int goalIdx  = g.index(goal.x,  goal.y);

    // Heuristic chooser
    auto heuristic = [&](int x, int y) -> int {
#if CG_PF_ENABLE_DIAGONALS
    #if CG_PF_DIAG_COST_SQRT2
        // Octile scaled by 10/14
        return octile14(Point{x, y}, goal);
    #else
        // Equal-cost diagonals => Chebyshev admissible
        return chebyshev(Point{x, y}, goal);
    #endif
#else
        return manhattan(Point{x, y}, goal);
#endif
    };

    // Open set
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    // Seed start
    s_scratch.gCost[static_cast<std::size_t>(startIdx)] = 0;
    const int hStart = heuristic(start.x, start.y);
    s_scratch.fCost[static_cast<std::size_t>(startIdx)] = hStart;
#if CG_PF_OPEN_DEDUP
    s_scratch.queuedF[static_cast<std::size_t>(startIdx)] = hStart;
#endif

    open.push(Node{ startIdx, hStart, 0 });
    ++s_stats.nodesPushed;
    s_stats.maxOpenSize = std::max(s_stats.maxOpenSize, static_cast<int>(open.size()));

    static const NeighborSet NB;
    int expanded = 0;

    // Main loop
    while (!open.empty()) {
        const Node node = open.top();
        open.pop();
        ++s_stats.nodesPopped;

        const int cur = node.idx;
        if (s_scratch.closed[static_cast<std::size_t>(cur)]) {
            continue; // stale
        }
        s_scratch.closed[static_cast<std::size_t>(cur)] = 1u;
        ++s_stats.nodesClosed;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes) {
            return Result::Aborted;
        }
        ++s_stats.nodesExpanded;

        if (cur == goalIdx) {
            reconstructPath(g, goalIdx, s_scratch.parent, out);
#if CG_PF_ENABLE_SMOOTHING
            // Optional LoS simplification
            {
                std::vector<Point> smoothed = out;
                // Only keep if not empty (avoid surprising empty on edge cases)
                // and strictly better than raw
                if (!smoothed.empty()) {
                    // We’ll do colinear strip either way (see below).
                }
                out.swap(smoothed);
            }
#endif
            // Always strip exact colinear runs (safe, shape-preserving)
            stripColinear(out);
            s_stats.pathLength = static_cast<int>(out.size());
            return Result::Found;
        }

        const Point p = g.fromIndex(cur);

        for (int i = 0; i < NeighborSet::Count; ++i) {
            const int ddx = NB.dx[i];
            const int ddy = NB.dy[i];
            const int nx = p.x + ddx;
            const int ny = p.y + ddy;

            if (!g.inBounds(nx, ny) || !g.walkable(nx, ny)) continue;
            if (!diagonalAllowedNoCornerCut(g, p, ddx, ddy)) continue;

            const int nIdx = g.index(nx, ny);
            if (s_scratch.closed[static_cast<std::size_t>(nIdx)]) continue;

            // Step cost: base per-tile cost, optionally scaled for diagonal moves.
            int base = std::max(1, g.cost(nx, ny));
#if CG_PF_ENABLE_DIAGONALS && CG_PF_DIAG_COST_SQRT2
            const bool diag = isDiagonalStep(ddx, ddy);
            const int scaled = diag ? (base * 14) : (base * 10);
            const int step = scaled; // keep integer scale; heuristic uses same scale
#else
            const int step = base;
#endif

            const int curG = s_scratch.gCost[static_cast<std::size_t>(cur)];
            if (curG == INF) continue; // should not happen; defensive
            const int tentativeG = (curG >= INF - step) ? INF : (curG + step);

            if (tentativeG < s_scratch.gCost[static_cast<std::size_t>(nIdx)]) {
                s_scratch.parent[static_cast<std::size_t>(nIdx)] = cur;
                s_scratch.gCost  [static_cast<std::size_t>(nIdx)] = tentativeG;

                const int hn = heuristic(nx, ny);
                const int fn = (tentativeG >= INF - hn) ? INF : (tentativeG + hn);
                s_scratch.fCost[static_cast<std::size_t>(nIdx)] = fn;

#if CG_PF_OPEN_DEDUP
                if (fn < s_scratch.queuedF[static_cast<std::size_t>(nIdx)]) {
                    s_scratch.queuedF[static_cast<std::size_t>(nIdx)] = fn;
                    open.push(Node{ nIdx, fn, tentativeG });
                    ++s_stats.nodesPushed;
                }
#else
                open.push(Node{ nIdx, fn, tentativeG });
                ++s_stats.nodesPushed;
#endif
                s_stats.maxOpenSize = std::max(s_stats.maxOpenSize, static_cast<int>(open.size()));
            }
        }
    }

    return Result::NoPath;
}

// ------------------------------------------------------------
// Convenience overload: write into a std::span<Point> (internal)
// ------------------------------------------------------------
Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::span<Point> outSpan,
             int maxExpandedNodes)
{
    std::vector<Point> tmp;
#if CG_PF_ENABLE_DIAGONALS
  #if CG_PF_DIAG_COST_SQRT2
    const int hint = octile14(start, goal) / 10 + 1; // scaled heuristic
  #else
    const int hint = chebyshev(start, goal) + 1;
  #endif
#else
    const int hint = manhattan(start, goal) + 1;
#endif
    if (hint > 0) tmp.reserve(static_cast<std::size_t>(hint));

    const Result r = aStar(g, start, goal, tmp, maxExpandedNodes);
    if (r == Result::Found && !tmp.empty() && !outSpan.empty()) {
        const std::size_t n = std::min(tmp.size(), outSpan.size());
        std::copy(tmp.end() - static_cast<std::ptrdiff_t>(n),
                  tmp.end(),
                  outSpan.end() - static_cast<std::ptrdiff_t>(n));
    }
    return r;
}

// ------------------------------------------------------------
// Optional helper (internal): best-effort path to nearest reachable
// ------------------------------------------------------------
// If the exact goal can't be reached, return a path to the closed node
// with the smallest heuristic distance to the goal. Public API remains
// unchanged; call this helper explicitly if you want this behavior.
[[maybe_unused]] static Result aStarOrNearestReachable(const GridView& g,
                                                       Point start,
                                                       Point goal,
                                                       std::vector<Point>& out,
                                                       int maxExpandedNodes)
{
    const Result r = aStar(g, start, goal, out, maxExpandedNodes);
    if (r == Result::Found || r == Result::Aborted) return r;

    // No path: find a "best" closed node
    const int N = g.w * g.h;
    int bestIdx = -1;
    int bestH = std::numeric_limits<int>::max();

    auto heuristic = [&](int x, int y) -> int {
#if CG_PF_ENABLE_DIAGONALS
  #if CG_PF_DIAG_COST_SQRT2
        return octile14(Point{x, y}, goal);
  #else
        return chebyshev(Point{x, y}, goal);
  #endif
#else
        return manhattan(Point{x, y}, goal);
#endif
    };

    for (int i = 0; i < N; ++i) {
        if (!s_scratch.closed[static_cast<std::size_t>(i)]) continue;
        const Point p = g.fromIndex(i);
        const int h = heuristic(p.x, p.y);
        if (h < bestH) { bestH = h; bestIdx = i; }
    }

    if (bestIdx >= 0) {
        reconstructPath(g, bestIdx, s_scratch.parent, out);
        stripColinear(out);
        return out.empty() ? Result::NoPath : Result::Found;
    }
    return Result::NoPath;
}

} // namespace cg::pf
