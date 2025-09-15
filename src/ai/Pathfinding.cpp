// src/ai/Pathfinding.cpp
//
// A* pathfinding with Windows-focused robustness and rich optional upgrades.
// Public API preserved:
//   Result aStar(const GridView& g, Point start, Point goal,
//                std::vector<Point>& out, int maxExpandedNodes)
//
// Massive upgrades over the base version:
// - Optional Bidirectional A* core (CG_PF_USE_BIDIRECTIONAL) for large maps.
// - Optional Weighted A* (CG_PF_WEIGHT) to trade optimality for speed.
// - Optional search window clamping around start/goal (CG_PF_BOUND_MARGIN).
// - Correct diagonals heuristic (Chebyshev or 10/14 octile for √2 costs).
// - Optional √2 diagonal movement (integer 10/14 scaling).
// - Colinear point stripping (ON by default) to reduce vertex count.
// - Optional Line-of-Sight smoothing pass (OFF by default).
// - Robust overflow handling; unity/jumbo TU friendly; MSVC-safe (NOMINMAX).
// - Thread-local search statistics (getLastSearchStats()) with timing & cost.
// - On abort due to node cap, optionally return the best partial path found.
//
// Compile-time flags (all default OFF unless noted):
//   CG_PF_ENABLE_DIAGONALS      0/1  (8-way movement allowed; default 0)
//   CG_PF_DIAG_COST_SQRT2       0/1  (8-way: diagonal cost ~1.414; 10/14 scale; default 0)
//   CG_PF_ENABLE_SMOOTHING      0/1  (post-process LoS smoothing; default 0)
//   CG_PF_OPEN_DEDUP            0/1  (avoid pushing worse/equal f for same node; default 1)
//   CG_PF_TIEBREAK_DEEPER_G     0/1  (prefer larger g on equal f; default 1)
//   CG_PF_STRIP_COLINEAR        0/1  (drop redundant colinear points; default 1)
//   CG_PF_USE_BIDIRECTIONAL     0/1  (use Bidirectional A*; default 0)
//   CG_PF_WEIGHT                int  (Weighted A*, f = g + W*h; default 1)
//   CG_PF_BOUND_MARGIN          int  (limit to bbox around start/goal ±margin; default 0)
//   CG_PF_RETURN_PARTIAL_ON_ABORT 0/1 (reconstruct best partial on abort; default 1)
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <limits>
#include <queue>
#if __has_include(<span>)
  #include <span>
#endif
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

#ifndef CG_PF_USE_BIDIRECTIONAL
#define CG_PF_USE_BIDIRECTIONAL 0
#endif

#ifndef CG_PF_WEIGHT
#define CG_PF_WEIGHT 1
#endif

#ifndef CG_PF_BOUND_MARGIN
#define CG_PF_BOUND_MARGIN 0
#endif

#ifndef CG_PF_RETURN_PARTIAL_ON_ABORT
#define CG_PF_RETURN_PARTIAL_ON_ABORT 1
#endif

static_assert(CG_PF_WEIGHT >= 1, "CG_PF_WEIGHT must be >= 1");

// ------------------------------
// Heuristics and helpers
// ------------------------------
static inline constexpr int absInt(int v) noexcept { return (v >= 0) ? v : -v; }

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

static inline bool isDiagonalStep(int dx, int dy) noexcept {
    return (dx != 0) && (dy != 0);
}

// ------------------------------
// Thread-local statistics
// ------------------------------
struct SearchStats {
    int nodesPushed     = 0;
    int nodesPopped     = 0;
    int nodesClosed     = 0;
    int nodesExpanded   = 0;
    int maxOpenSize     = 0;
    int pathLength      = 0; // number of waypoints in result
    long long timeMicros= 0; // wall-clock microseconds
    int pathCost        = 0; // final g-cost (scaled if DIAG_COST_SQRT2)
    unsigned algoFlags  = 0; // bit0=diag, bit1=sqrt2, bit2=bidir, bit3=weighted(W>1)
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

#if CG_PF_USE_BIDIRECTIONAL
struct ScratchBi {
    std::vector<int> gF, gB;
    std::vector<int> fF, fB;
    std::vector<int> parentF, parentB;
    std::vector<unsigned char> closedF, closedB;
#if CG_PF_OPEN_DEDUP
    std::vector<int> queuedFF, queuedFB;
#endif
    void reset(std::size_t N, int INF) {
        auto init = [&](std::vector<int>& v, int val) {
            if (v.size() != N) v.assign(N, val); else std::fill(v.begin(), v.end(), val);
        };
        auto initb = [&](std::vector<unsigned char>& v) {
            if (v.size() != N) v.assign(N, 0u); else std::fill(v.begin(), v.end(), 0u);
        };
        init(gF, INF);  init(gB, INF);
        init(fF, INF);  init(fB, INF);
        if (parentF.size() != N) parentF.assign(N, -1); else std::fill(parentF.begin(), parentF.end(), -1);
        if (parentB.size() != N) parentB.assign(N, -1); else std::fill(parentB.begin(), parentB.end(), -1);
        initb(closedF); initb(closedB);
    #if CG_PF_OPEN_DEDUP
        init(queuedFF, INF); init(queuedFB, INF);
    #endif
    }
};
static thread_local ScratchBi s_bi;
#endif

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
        // 8-way (E, W, N, S, NE, SE, NW, SW)
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

static void smoothPath(const GridView& g, std::vector<Point>& path) {
    if (path.size() < 3) return;
    std::vector<Point> smoothed;
    smoothed.reserve(path.size());
    std::size_t i = 0;
    smoothed.push_back(path[0]);
    while (i + 1 < path.size()) {
        std::size_t k = i + 1;
        while (k + 1 < path.size() && lineOfSight(g, path[i], path[k + 1])) {
            ++k;
        }
        smoothed.push_back(path[k]);
        i = k;
    }
    path.swap(smoothed);
}
#endif

// Always strip strictly colinear midpoints: ... A, B, C ... where (B - A) × (C - B) == 0
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
// Utility: RAII timer
// ------------------------------
struct Timer {
    using clock = std::chrono::steady_clock;
    clock::time_point t0;
    Timer() : t0(clock::now()) {}
    long long micros() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - t0).count();
    }
};

// ------------------------------
// Step cost (handles scaling)
// ------------------------------
static inline int stepCost(const GridView& g, int ddx, int ddy, int nx, int ny) {
    int base = ((std::max))(1, g.cost(nx, ny));
#if CG_PF_ENABLE_DIAGONALS && CG_PF_DIAG_COST_SQRT2
    const bool diag = isDiagonalStep(ddx, ddy);
    return diag ? (base * 14) : (base * 10);
#else
    (void)ddx; (void)ddy;
    return base;
#endif
}

// ------------------------------
// Search window clamping
// ------------------------------
struct Bounds {
    int minX, maxX, minY, maxY;
    bool contains(int x, int y) const noexcept {
        return (x >= minX && x <= maxX && y >= minY && y <= maxY);
    }
};

static inline Bounds computeSearchBounds(const GridView& g, Point s, Point t) {
#if CG_PF_BOUND_MARGIN <= 0
    (void)s; (void)t;
    return Bounds{0, g.w - 1, 0, g.h - 1};
#else
    const int m = CG_PF_BOUND_MARGIN;
    int minX = ((std::min))(s.x, t.x) - m;
    int maxX = ((std::max))(s.x, t.x) + m;
    int minY = ((std::min))(s.y, t.y) - m;
    int maxY = ((std::max))(s.y, t.y) + m;
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > g.w - 1) maxX = g.w - 1;
    if (maxY > g.h - 1) maxY = g.h - 1;
    return Bounds{minX, maxX, minY, maxY};
#endif
}

static inline bool inBoundsWindowed(const GridView& g, int x, int y, const Bounds& b) {
    return b.contains(x, y) && g.inBounds(x, y);
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

// Reconstruct bidirectional: start -> meet + reversed(meet -> goal)
#if CG_PF_USE_BIDIRECTIONAL
static void reconstructBiPath(const GridView& g,
                              int meetIdx,
                              const std::vector<int>& parentF,
                              const std::vector<int>& parentB,
                              std::vector<Point>& out)
{
    std::vector<Point> left, right;
    reconstructPath(g, meetIdx, parentF, left);
    // From meet to goal using parentB (which points "toward" the goal's tree root)
    std::vector<Point> revRight;
    for (int p = meetIdx; p != -1; p = parentB[static_cast<std::size_t>(p)]) {
        revRight.push_back(g.fromIndex(p));
    }
    // revRight is [meet ... goal]; drop meet to avoid duplication, then append reversed
    if (!revRight.empty()) revRight.pop_back();
    right.assign(revRight.rbegin(), revRight.rend());

    out.clear();
    out.reserve(left.size() + right.size());
    out.insert(out.end(), left.begin(), left.end());
    out.insert(out.end(), right.begin(), right.end());
}
#endif

// ------------------------------------
// Unidirectional A* (baseline, upgraded)
// ------------------------------------
static Result aStarUni(const GridView& g,
                       Point start,
                       Point goal,
                       std::vector<Point>& out,
                       int maxExpandedNodes)
{
    constexpr int W = CG_PF_WEIGHT;

    Timer timer;
    s_stats = {};
#if CG_PF_ENABLE_DIAGONALS
    s_stats.algoFlags |= 1u << 0;
#endif
#if CG_PF_DIAG_COST_SQRT2
    s_stats.algoFlags |= 1u << 1;
#endif
    if (W > 1) s_stats.algoFlags |= 1u << 3;

    out.clear();

    // Basic checks
    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) {
        s_stats.timeMicros = timer.micros();
        return Result::NoPath;
    }
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) {
        s_stats.timeMicros = timer.micros();
        return Result::NoPath;
    }
    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        s_stats.pathLength = 1;
        s_stats.pathCost   = 0;
        s_stats.timeMicros = timer.micros();
        return Result::Found;
    }

    const int N = g.w * g.h;
    if (N <= 0) { s_stats.timeMicros = timer.micros(); return Result::NoPath; }

    constexpr int INF = std::numeric_limits<int>::max();
    s_scratch.reset(static_cast<std::size_t>(N), INF);

    const int startIdx = g.index(start.x, start.y);
    const int goalIdx  = g.index(goal.x,  goal.y);

    const Bounds bounds = computeSearchBounds(g, start, goal);

    // Heuristic chooser
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

    // Open set
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    // Seed start
#if CG_PF_DIAG_COST_SQRT2
    const int gStart = 0;
#else
    const int gStart = 0;
#endif
    s_scratch.gCost[static_cast<std::size_t>(startIdx)] = gStart;
    const int hStart = heuristic(start.x, start.y);
    const int fStart = (hStart >= INF / W) ? INF : (gStart + W * hStart);
    s_scratch.fCost[static_cast<std::size_t>(startIdx)] = fStart;
#if CG_PF_OPEN_DEDUP
    s_scratch.queuedF[static_cast<std::size_t>(startIdx)] = fStart;
#endif

    open.push(Node{ startIdx, fStart, gStart });
    ++s_stats.nodesPushed;
    s_stats.maxOpenSize = ((std::max))(s_stats.maxOpenSize, static_cast<int>(open.size()));

    static const NeighborSet NB;
    int expanded = 0;
    int lastBestIdx = startIdx; // for partial-path extraction on abort

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
        lastBestIdx = cur;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes) {
#if CG_PF_RETURN_PARTIAL_ON_ABORT
            reconstructPath(g, lastBestIdx, s_scratch.parent, out);
    #if CG_PF_ENABLE_SMOOTHING
            // optional smoothing only on sufficiently long partials
            if (out.size() > 4) smoothPath(g, out);
    #endif
            stripColinear(out);
            s_stats.pathLength = static_cast<int>(out.size());
            const int goalG = s_scratch.gCost[static_cast<std::size_t>(lastBestIdx)];
            s_stats.pathCost = (goalG == INF) ? 0 : goalG;
#endif
            s_stats.timeMicros = timer.micros();
            return Result::Aborted;
        }
        ++s_stats.nodesExpanded;

        if (cur == goalIdx) {
            reconstructPath(g, goalIdx, s_scratch.parent, out);
#if CG_PF_ENABLE_SMOOTHING
            smoothPath(g, out);
#endif
            stripColinear(out);
            s_stats.pathLength = static_cast<int>(out.size());
            s_stats.pathCost   = s_scratch.gCost[static_cast<std::size_t>(goalIdx)];
            s_stats.timeMicros = timer.micros();
            return Result::Found;
        }

        const Point p = g.fromIndex(cur);

        for (int i = 0; i < NeighborSet::Count; ++i) {
            const int ddx = NB.dx[i];
            const int ddy = NB.dy[i];
            const int nx = p.x + ddx;
            const int ny = p.y + ddy;

            if (!inBoundsWindowed(g, nx, ny, bounds) || !g.walkable(nx, ny)) continue;
            if (!diagonalAllowedNoCornerCut(g, p, ddx, ddy)) continue;

            const int nIdx = g.index(nx, ny);
            if (s_scratch.closed[static_cast<std::size_t>(nIdx)]) continue;

            const int step = stepCost(g, ddx, ddy, nx, ny);
            const int curG = s_scratch.gCost[static_cast<std::size_t>(cur)];
            if (curG == INF) continue; // defensive
            const int tentativeG = (curG >= INF - step) ? INF : (curG + step);

            if (tentativeG < s_scratch.gCost[static_cast<std::size_t>(nIdx)]) {
                s_scratch.parent[static_cast<std::size_t>(nIdx)] = cur;
                s_scratch.gCost  [static_cast<std::size_t>(nIdx)] = tentativeG;

                const int hn = heuristic(nx, ny);
                const int wh = (hn >= INF / W) ? INF : (W * hn);
                const int fn = (tentativeG >= INF - wh) ? INF : (tentativeG + wh);
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
                s_stats.maxOpenSize = ((std::max))(s_stats.maxOpenSize, static_cast<int>(open.size()));
            }
        }
    }

    s_stats.timeMicros = timer.micros();
    return Result::NoPath;
}

// ------------------------------------
// Bidirectional A* (optional)
// ------------------------------------
#if CG_PF_USE_BIDIRECTIONAL
static Result aStarBi(const GridView& g,
                      Point start,
                      Point goal,
                      std::vector<Point>& out,
                      int maxExpandedNodes)
{
    constexpr int W = CG_PF_WEIGHT;

    Timer timer;
    s_stats = {};
    s_stats.algoFlags |= 1u << 2; // bidir
#if CG_PF_ENABLE_DIAGONALS
    s_stats.algoFlags |= 1u << 0;
#endif
#if CG_PF_DIAG_COST_SQRT2
    s_stats.algoFlags |= 1u << 1;
#endif
    if (W > 1) s_stats.algoFlags |= 1u << 3;

    out.clear();

    if (!g.inBounds(start.x, start.y) || !g.inBounds(goal.x, goal.y)) {
        s_stats.timeMicros = timer.micros();
        return Result::NoPath;
    }
    if (!g.walkable(start.x, start.y) || !g.walkable(goal.x, goal.y)) {
        s_stats.timeMicros = timer.micros();
        return Result::NoPath;
    }
    if (start.x == goal.x && start.y == goal.y) {
        out.push_back(start);
        s_stats.pathLength = 1;
        s_stats.pathCost   = 0;
        s_stats.timeMicros = timer.micros();
        return Result::Found;
    }

    const int N = g.w * g.h;
    if (N <= 0) { s_stats.timeMicros = timer.micros(); return Result::NoPath; }

    constexpr int INF = std::numeric_limits<int>::max();
    s_bi.reset(static_cast<std::size_t>(N), INF);

    const int sIdx = g.index(start.x, start.y);
    const int tIdx = g.index(goal.x,  goal.y);

    const Bounds bounds = computeSearchBounds(g, start, goal);

    auto hF = [&](int x, int y) -> int {
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
    auto hB = [&](int x, int y) -> int {
#if CG_PF_ENABLE_DIAGONALS
  #if CG_PF_DIAG_COST_SQRT2
        return octile14(Point{x, y}, start);
  #else
        return chebyshev(Point{x, y}, start);
  #endif
#else
        return manhattan(Point{x, y}, start);
#endif
    };

    std::priority_queue<Node, std::vector<Node>, NodeCmp> openF, openB;

    s_bi.gF[static_cast<std::size_t>(sIdx)] = 0;
    const int hf = hF(start.x, start.y);
    const int ff = (hf >= INF / W) ? INF : (W * hf);
    s_bi.fF[static_cast<std::size_t>(sIdx)] = ff;
#if CG_PF_OPEN_DEDUP
    s_bi.queuedFF[static_cast<std::size_t>(sIdx)] = ff;
#endif
    openF.push(Node{ sIdx, ff, 0 });
    ++s_stats.nodesPushed;

    s_bi.gB[static_cast<std::size_t>(tIdx)] = 0;
    const int hb = hB(goal.x, goal.y);
    const int fb = (hb >= INF / W) ? INF : (W * hb);
    s_bi.fB[static_cast<std::size_t>(tIdx)] = fb;
#if CG_PF_OPEN_DEDUP
    s_bi.queuedFB[static_cast<std::size_t>(tIdx)] = fb;
#endif
    openB.push(Node{ tIdx, fb, 0 });
    ++s_stats.nodesPushed;

    static const NeighborSet NB;
    int expanded = 0;
    int bestMeet = -1;
    int bestMeetCost = INF;

    auto relaxSide = [&](bool forward) -> bool {
        auto& open   = forward ? openF : openB;
        auto& gCost  = forward ? s_bi.gF : s_bi.gB;
        auto& fCost  = forward ? s_bi.fF : s_bi.fB;
        auto& parent = forward ? s_bi.parentF : s_bi.parentB;
        auto& closed = forward ? s_bi.closedF : s_bi.closedB;
#if CG_PF_OPEN_DEDUP
        auto& queued = forward ? s_bi.queuedFF : s_bi.queuedFB;
#endif
        const auto& gOther  = forward ? s_bi.gB : s_bi.gF;
        const auto& closedOther = forward ? s_bi.closedB : s_bi.closedF;
        auto h = forward ? hF : hB;

        if (open.empty()) return false;

        const Node node = open.top();
        open.pop();
        ++s_stats.nodesPopped;

        const int cur = node.idx;
        if (closed[static_cast<std::size_t>(cur)]) {
            return true; // continue outer loop
        }
        closed[static_cast<std::size_t>(cur)] = 1u;
        ++s_stats.nodesClosed;

        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes) {
#if CG_PF_RETURN_PARTIAL_ON_ABORT
            if (bestMeet >= 0) {
                reconstructBiPath(g, bestMeet, s_bi.parentF, s_bi.parentB, out);
                stripColinear(out);
                s_stats.pathLength = static_cast<int>(out.size());
                s_stats.pathCost   = bestMeetCost;
            }
#endif
            s_stats.timeMicros = timer.micros();
            return false; // signal abort (outer will treat as Aborted)
        }
        ++s_stats.nodesExpanded;

        const Point p = g.fromIndex(cur);
        for (int i = 0; i < NeighborSet::Count; ++i) {
            const int ddx = NB.dx[i];
            const int ddy = NB.dy[i];
            const int nx = p.x + ddx;
            const int ny = p.y + ddy;

            if (!inBoundsWindowed(g, nx, ny, bounds) || !g.walkable(nx, ny)) continue;
            if (!diagonalAllowedNoCornerCut(g, p, ddx, ddy)) continue;

            const int nIdx = g.index(nx, ny);
            if (closed[static_cast<std::size_t>(nIdx)]) continue;

            const int step = stepCost(g, ddx, ddy, nx, ny);
            const int curG = gCost[static_cast<std::size_t>(cur)];
            if (curG == INF) continue;
            const int tentativeG = (curG >= INF - step) ? INF : (curG + step);

            if (tentativeG < gCost[static_cast<std::size_t>(nIdx)]) {
                parent[static_cast<std::size_t>(nIdx)] = cur;
                gCost [static_cast<std::size_t>(nIdx)] = tentativeG;

                const int hn = h(nx, ny);
                const int wh = (hn >= INF / W) ? INF : (W * hn);
                const int fn = (tentativeG >= INF - wh) ? INF : (tentativeG + wh);
                fCost[static_cast<std::size_t>(nIdx)] = fn;

#if CG_PF_OPEN_DEDUP
                if (fn < queued[static_cast<std::size_t>(nIdx)]) {
                    queued[static_cast<std::size_t>(nIdx)] = fn;
                    open.push(Node{ nIdx, fn, tentativeG });
                    ++s_stats.nodesPushed;
                }
#else
                open.push(Node{ nIdx, fn, tentativeG });
                ++s_stats.nodesPushed;
#endif
                s_stats.maxOpenSize = ((std::max))(s_stats.maxOpenSize, static_cast<int>(((std::max))(openF.size(), openB.size())));
            }

            // Meeting check: if the other side has already closed this node, we have a connection
            if (closedOther[static_cast<std::size_t>(nIdx)]) {
                const int cost = gCost[static_cast<std::size_t>(nIdx)] + gOther[static_cast<std::size_t>(nIdx)];
                if (cost < bestMeetCost) {
                    bestMeet = nIdx;
                    bestMeetCost = cost;
                }
            }
        }

        return true;
    };

    // Alternate expanding the fronts (forward/backward)
    while (!openF.empty() || !openB.empty()) {
        if (!relaxSide(true)) {
            if (expanded > maxExpandedNodes && maxExpandedNodes >= 0) {
                s_stats.timeMicros = timer.micros();
                return Result::Aborted;
            }
            break;
        }
        if (bestMeet >= 0) {
            reconstructBiPath(g, bestMeet, s_bi.parentF, s_bi.parentB, out);
#if CG_PF_ENABLE_SMOOTHING
            smoothPath(g, out);
#endif
            stripColinear(out);
            s_stats.pathLength = static_cast<int>(out.size());
            s_stats.pathCost   = bestMeetCost;
            s_stats.timeMicros = timer.micros();
            return Result::Found;
        }
        if (!relaxSide(false)) {
            if (expanded > maxExpandedNodes && maxExpandedNodes >= 0) {
                s_stats.timeMicros = timer.micros();
                return Result::Aborted;
            }
            break;
        }
        if (bestMeet >= 0) {
            reconstructBiPath(g, bestMeet, s_bi.parentF, s_bi.parentB, out);
#if CG_PF_ENABLE_SMOOTHING
            smoothPath(g, out);
#endif
            stripColinear(out);
            s_stats.pathLength = static_cast<int>(out.size());
            s_stats.pathCost   = bestMeetCost;
            s_stats.timeMicros = timer.micros();
            return Result::Found;
        }
    }

    s_stats.timeMicros = timer.micros();
    return Result::NoPath;
}
#endif // CG_PF_USE_BIDIRECTIONAL

// ------------------------------------
// Public API (unchanged signature)
// ------------------------------------
Result aStar(const GridView& g,
             Point start,
             Point goal,
             std::vector<Point>& out,
             int maxExpandedNodes)
{
#if CG_PF_USE_BIDIRECTIONAL
    return aStarBi(g, start, goal, out, maxExpandedNodes);
#else
    return aStarUni(g, start, goal, out, maxExpandedNodes);
#endif
}

// ------------------------------------------------------------
// Convenience overload: write into a std::span<Point> (internal)
// ------------------------------------------------------------
#if __has_include(<span>)
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
        const std::size_t n = ((std::min))(tmp.size(), outSpan.size());
        std::copy(tmp.end() - static_cast<std::ptrdiff_t>(n),
                  tmp.end(),
                  outSpan.end() - static_cast<std::ptrdiff_t>(n));
    }
    return r;
}
#endif

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

    // No path: find a "best" closed node using the *unidirectional* scratch if available
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

#if CG_PF_USE_BIDIRECTIONAL
    // Prefer the side with more closes; scan both otherwise.
    int closedCountF = 0, closedCountB = 0;
    for (int i = 0; i < N; ++i) { if (s_bi.closedF[static_cast<std::size_t>(i)]) ++closedCountF; }
    for (int i = 0; i < N; ++i) { if (s_bi.closedB[static_cast<std::size_t>(i)]) ++closedCountB; }
    const bool scanF = closedCountF >= closedCountB;
    if (scanF) {
        for (int i = 0; i < N; ++i) {
            if (!s_bi.closedF[static_cast<std::size_t>(i)]) continue;
            const Point p = g.fromIndex(i);
            const int h = heuristic(p.x, p.y);
            if (h < bestH) { bestH = h; bestIdx = i; }
        }
        if (bestIdx >= 0) {
            reconstructPath(g, bestIdx, s_bi.parentF, out);
            stripColinear(out);
            return out.empty() ? Result::NoPath : Result::Found;
        }
    }
    // Fallback to B side
    for (int i = 0; i < N; ++i) {
        if (!s_bi.closedB[static_cast<std::size_t>(i)]) continue;
        const Point p = g.fromIndex(i);
        const int h = heuristic(p.x, p.y);
        if (h < bestH) { bestH = h; bestIdx = i; }
    }
    if (bestIdx >= 0) {
        // Build from start to bestIdx via F if we have a parentF chain,
        // otherwise reverse B then reverse the path (approximate).
        if (s_bi.parentF[static_cast<std::size_t>(bestIdx)] != -1) {
            reconstructPath(g, bestIdx, s_bi.parentF, out);
        } else {
            std::vector<Point> rev;
            for (int p = bestIdx; p != -1; p = s_bi.parentB[static_cast<std::size_t>(p)]) {
                rev.push_back(g.fromIndex(p));
            }
            out.assign(rev.rbegin(), rev.rend());
        }
        stripColinear(out);
        return out.empty() ? Result::NoPath : Result::Found;
    }
#else
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
#endif

    return Result::NoPath;
}

} // namespace cg::pf
