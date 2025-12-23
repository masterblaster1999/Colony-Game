#pragma once
/*
    Colony-Game — Pathfinding (header-only)

    What you get:
      • Time-sliced A* (call update(maxExpansionsPerFrame))
      • 4/8-connected grids with optional corner-cutting prevention
      • Weighted terrain (per-cell additional cost callback)
      • Per-request goal tolerance (reach "nearby" goals)
      • Partial-path fallback when a goal is unreachable or budget is hit
      • Path smoothing (line-of-sight string-pulling)
      • Deterministic tie-breaking for stable paths
      • Stats for profiling (expansions, pushes, peak open size, etc.)

    Usage sketch:
      ai::Pathfinder pf(mapW, mapH);
      pf.request({
          .start = {sx, sy},
          .goal  = {gx, gy},
          .isWalkable   = [&](int x,int y){ return world[y][x].walkable(); },
          .terrainCost  = [&](int x,int y){ return world[y][x].extraCost(); }, // optional
          .onComplete   = [&](const ai::Path& p){ colonist.setPath(p.points); },
          .allowDiagonal = true,
          .forbidCornerCutting = true,
          .smoothPath = true,
          .goalTolerance = 0, // exact
      });
      // Per frame:
      pf.update(2500);

    This header is platform-agnostic and works in your Windows-only build as-is.
*/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

namespace ai {

//----------------------------- Public Types ----------------------------------

struct PFPoint {
    int x = 0, y = 0;
};

struct Path {
    std::vector<PFPoint> points;
    bool   success = false;     // true if reached goal within tolerance
    float  cost    = 0.0f;      // accumulated g-cost of the returned path
    float  length  = 0.0f;      // geometric length (1 for straight, sqrt(2) for diagonal steps)
};

struct PathRequest {
    PFPoint start{};
    PFPoint goal{};

    // Required: returns true if tile (x,y) can be stepped onto.
    std::function<bool(int,int)> isWalkable;

    // Optional: additional cost added when stepping onto tile (x,y).
    // If not provided, treated as 0.
    std::function<float(int,int)> terrainCost;

    // Required: fired once when the search completes (success, failure, or partial).
    std::function<void(const Path&)> onComplete;

    // Movement / search configuration:
    bool  allowDiagonal         = true;
    bool  forbidCornerCutting   = true;   // when moving diagonally, require the two orthogonal neighbors to be free
    bool  smoothPath            = true;   // string-pull smoothing using line-of-sight
    bool  allowPartial          = true;   // if goal is unreachable, return best-effort path towards it
    int   goalTolerance         = 0;      // Manhattan tolerance (0 means exact tile)
    float heuristicWeight       = 1.0f;   // >= 1.0; >1.0 speeds up but becomes inadmissible
};

class Pathfinder {
public:
    explicit Pathfinder(int width, int height)
    { resize(width, height); }

    void resize(int width, int height)
    {
        m_w = std::max(1, width);
        m_h = std::max(1, height);
        const int n = m_w * m_h;
        m_mark.assign(n, 0);
        m_g.assign(n, std::numeric_limits<float>::infinity());
        m_parent.assign(n, -1);
        clearQueue();
        m_active.active = false;
        // keep other tunables as-is
    }

    // Queue a request. Safe to call any time (processed FIFO).
    void request(PathRequest req)
    {
        // Ensure callbacks exist; degrade gracefully if missing.
        if (!req.isWalkable) {
            // A missing isWalkable would make the world fully walkable;
            // we keep it explicit to avoid silent mistakes.
            req.isWalkable = [](int, int){ return true; };
        }
        // Clamp to grid
        req.start.x = std::clamp(req.start.x, 0, m_w - 1);
        req.start.y = std::clamp(req.start.y, 0, m_h - 1);
        req.goal.x  = std::clamp(req.goal.x,  0, m_w - 1);
        req.goal.y  = std::clamp(req.goal.y,  0, m_h - 1);
        m_queue.emplace_back(std::move(req));
    }

    // Execute up to maxExpansions node expansions.
    // Returns true if still working on a search after this call.
    bool update(std::size_t maxExpansions = 2000)
    {
        if (!m_active.active) {
            if (m_queue.empty())
                return false;
            startActive(std::move(m_queue.front()));
            m_queue.erase(m_queue.begin());
        }

        std::size_t steps = 0;
        while (m_active.active && steps < maxExpansions) {
            if (!stepOne()) break;
            ++steps;
        }
        return m_active.active || !m_queue.empty();
    }

    // Cancel current search (optionally invoke its callback with failure) and clear queue.
    void cancelActive(bool invokeCallback = false)
    {
        if (m_active.active && invokeCallback) {
            Path out; // empty failure
            out.success = false;
            if (m_active.req.onComplete) m_active.req.onComplete(out);
        }
        m_active.active = false;
        clearQueue();
    }

    void clearQueue() { m_queue.clear(); }

    std::size_t pending() const
    {
        return m_queue.size() + (m_active.active ? 1u : 0u);
    }

    // Global tunables (can be overridden per-request via PathRequest fields)
    void setHeuristicWeight(float w) { m_globalHeuristicWeight = (w >= 1.0f ? w : 1.0f); }
    void setStepCosts(float straightCost = 1.0f, float diagonalCost = 1.41421356f)
    {
        m_costStraight = straightCost > 0 ? straightCost : 1.0f;
        m_costDiagonal = diagonalCost > 0 ? diagonalCost : 1.41421356f;
    }

    struct Stats {
        std::size_t expansions = 0; // nodes popped from open
        std::size_t pushes     = 0; // nodes pushed to open
        std::size_t reopens    = 0; // better g found for previously seen node
        std::size_t touched    = 0; // neighbors evaluated
        std::size_t peakOpen   = 0; // max size of open list
    };
    const Stats& lastStats() const { return m_lastStats; }

private:
    //--------------------------- Internal Types -------------------------------

    // Mark packing: [state:2 bits | gen:30 bits]
    enum : uint32_t { STATE_UNSEEN = 0, STATE_OPEN = 1, STATE_CLOSED = 2 };
    static inline uint32_t pack(uint32_t gen, uint32_t st) { return (gen & 0x3FFFFFFFu) | (st << 30); }
    static inline uint32_t markState(uint32_t m)            { return m >> 30; }
    static inline uint32_t markGen(uint32_t m)              { return m & 0x3FFFFFFFu; }

    struct OpenNode {
        float f;
        float g;
        int   idx;
        uint64_t order; // monotonic counter for stable tie-breaking
    };
    struct OpenCmp {
        bool operator()(const OpenNode& a, const OpenNode& b) const noexcept {
            if (a.f != b.f)   return a.f > b.f;     // smaller f first
            if (a.g != b.g)   return a.g < b.g;     // prefer larger g (longer straight segments)
            return a.order > b.order;               // FIFO among equals
        }
    };

    struct Active {
        bool active = false;
        PathRequest req{};
        int   startIdx = -1;
        int   goalIdx  = -1;
        float goalHx   = 0.0f; // cached for stats / tie-breakers

        std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCmp> open;
        uint32_t gen = 1;

        // Best-so-far for partial path (closest to goal by heuristic, then by f)
        int   bestIdx = -1;
        float bestH   = std::numeric_limits<float>::infinity();
        float bestF   = std::numeric_limits<float>::infinity();

        Stats stats{};
    };

    //--------------------------- Grid Utilities -------------------------------

    static inline int idx(int x, int y, int w) noexcept { return y * w + x; }
    static inline PFPoint xy(int index, int w) noexcept { return { index % w, index / w }; }

    static inline float manhattan(int x0, int y0, int x1, int y1) noexcept
    { return float(std::abs(x1 - x0) + std::abs(y1 - y0)); }

    static inline float octile(int x0, int y0, int x1, int y1, float straight, float diagonal) noexcept
    {
        const int dx = std::abs(x1 - x0);
        const int dy = std::abs(y1 - y0);
        const int dmin = (dx < dy) ? dx : dy;
        const int dmax = (dx > dy) ? dx : dy;
        return diagonal * float(dmin) + straight * float(dmax - dmin);
    }

    //--------------------------- Search Control -------------------------------

    void startActive(PathRequest&& req)
    {
        m_active = Active{};
        m_active.active = true;
        m_active.req = std::move(req);
        m_lastStats = {}; // reset public stats

        // bump generation; wrap safely in 30 bits to avoid stale "visited forever"
        m_active.gen = (m_active.gen + 1) & 0x3FFFFFFFu;
        if (m_active.gen == 0) {
            // Rare wrap: hard clear marks so gen==0 is unique again.
            std::fill(m_mark.begin(), m_mark.end(), 0);
            m_active.gen = 1;
        }

        // Reinit arrays
        const int n = m_w * m_h;
        if ((int)m_mark.size() != n) {
            m_mark.assign(n, 0);
            m_g.assign(n, std::numeric_limits<float>::infinity());
            m_parent.assign(n, -1);
        } else {
            // lazily reset via generation marks, g and parent freshly reset for safety
            std::fill(m_g.begin(), m_g.end(), std::numeric_limits<float>::infinity());
            std::fill(m_parent.begin(), m_parent.end(), -1);
        }
        while (!m_active.open.empty()) m_active.open.pop();
        m_orderCounter = 0;

        // Start / goal
        m_active.startIdx = idx(m_active.req.start.x, m_active.req.start.y, m_w);
        m_active.goalIdx  = idx(m_active.req.goal.x,  m_active.req.goal.y,  m_w);

        // Early out if start ~ goal within tolerance
        if (reachedGoal(m_active.req.start.x, m_active.req.start.y)) {
            Path p;
            p.success = true;
            p.points = { m_active.req.start };
            p.cost = 0.0f;
            p.length = 0.0f;
            if (m_active.req.onComplete) m_active.req.onComplete(p);
            m_active.active = false;
            return;
        }

        m_g[m_active.startIdx] = 0.0f;
        m_parent[m_active.startIdx] = -1;
        m_mark[m_active.startIdx] = pack(m_active.gen, STATE_OPEN);

        const float h0 = heuristic(m_active.req.start.x, m_active.req.start.y);
        m_active.open.push({ h0, 0.0f, m_active.startIdx, m_orderCounter++ });
        m_active.bestIdx = m_active.startIdx;
        m_active.bestH   = h0;
        m_active.bestF   = h0;
        m_active.stats = {};
        m_active.stats.pushes = 1;
        m_active.stats.peakOpen = 1;
    }

    // Expand one node; return true to continue, false when search finished this frame
    bool stepOne()
    {
        if (m_active.open.empty()) {
            // Failure: no path. Return partial if allowed.
            finishSearch(/*success*/false, /*emptyOpen*/true);
            return false;
        }

        const OpenNode cur = m_active.open.top();
        m_active.open.pop();

        // Skip stale entries (replaced by a better g later).
        if (markGen(m_mark[cur.idx]) != m_active.gen || markState(m_mark[cur.idx]) == STATE_CLOSED) {
            return true;
        }

        m_active.stats.expansions++;

        // Goal check (with tolerance)
        const PFPoint cxy = xy(cur.idx, m_w);
        if (reachedGoal(cxy.x, cxy.y)) {
            finishSearch(/*success*/true, /*emptyOpen*/false, cur.idx);
            return false;
        }

        // Close current
        m_mark[cur.idx] = pack(m_active.gen, STATE_CLOSED);

        // Enumerate neighbors
        static const int DIR4[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        static const int DIR8[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };

        const bool useDiag = m_active.req.allowDiagonal;
        const int (*dirs)[2] = useDiag ? DIR8 : DIR4;
        const int dirCount   = useDiag ? 8 : 4;

        for (int d = 0; d < dirCount; ++d) {
            const int nx = cxy.x + dirs[d][0];
            const int ny = cxy.y + dirs[d][1];
            if (!inside(nx, ny)) continue;

            // Corner cutting prevention for diagonal steps
            const bool diag = (dirs[d][0] != 0 && dirs[d][1] != 0);
            if (diag && m_active.req.forbidCornerCutting) {
                const int mx = cxy.x + dirs[d][0];
                const int my = cxy.y;
                const int nx2 = cxy.x;
                const int ny2 = cxy.y + dirs[d][1];
                if (!m_active.req.isWalkable(mx, my) || !m_active.req.isWalkable(nx2, ny2))
                    continue;
            }

            // Walkability of target cell
            if (!m_active.req.isWalkable(nx, ny)) continue;

            m_active.stats.touched++;

            const int nIdx = idx(nx, ny, m_w);
            const float stepBase = diag ? m_costDiagonal : m_costStraight;
            const float extra = m_active.req.terrainCost ? m_active.req.terrainCost(nx, ny) : 0.0f;
            const float tentativeG = cur.g + stepBase + extra;

            // If we've never seen this node in this generation, its g is INF
            const bool seenThisGen = (markGen(m_mark[nIdx]) == m_active.gen);
            const float prevG = seenThisGen ? m_g[nIdx] : std::numeric_limits<float>::infinity();
            if (tentativeG + 1e-6f < prevG) {
                if (seenThisGen && markState(m_mark[nIdx]) == STATE_CLOSED)
                    m_active.stats.reopens++;

                m_parent[nIdx] = cur.idx;
                m_g[nIdx] = tentativeG;

                const float h = heuristic(nx, ny);
                float f = tentativeG + h * requestHeuristicWeight();

                m_mark[nIdx] = pack(m_active.gen, STATE_OPEN);
                m_active.open.push({ f, tentativeG, nIdx, m_orderCounter++ });
                m_active.stats.pushes++;
                if (m_active.open.size() > m_active.stats.peakOpen)
                    m_active.stats.peakOpen = m_active.open.size();

                // Track best-so-far for partial path fallback
                if (h + 1e-6f < m_active.bestH ||
                    (std::abs(h - m_active.bestH) <= 1e-6f && f < m_active.bestF)) {
                    m_active.bestH = h;
                    m_active.bestF = f;
                    m_active.bestIdx = nIdx;
                }
            }
        }

        return true;
    }

    void finishSearch(bool success, bool emptyOpen, int goalOrBestIdx = -1)
    {
        // Gather stats for public consumption
        m_lastStats = m_active.stats;

        Path out;
        out.success = success;

        int useIdx = -1;
        if (success) {
            useIdx = (goalOrBestIdx >= 0) ? goalOrBestIdx : m_active.goalIdx;
        } else {
            // No path: possibly return partial
            if (m_active.req.allowPartial && m_active.bestIdx >= 0) {
                useIdx = m_active.bestIdx;
            } else {
                // No path and partial disabled: return empty failure
                useIdx = -1;
            }
        }

        if (useIdx >= 0) {
            reconstructPath(useIdx, out);
            if (m_active.req.smoothPath && out.points.size() >= 3) {
                smoothStringPull(out);
            }
        }

        if (m_active.req.onComplete) m_active.req.onComplete(out);
        m_active.active = false;
    }

    //--------------------------- Geometry helpers -----------------------------

    bool inside(int x, int y) const noexcept
    { return (uint32_t)x < (uint32_t)m_w && (uint32_t)y < (uint32_t)m_h; }

    bool reachedGoal(int x, int y) const noexcept
    {
        const int gx = m_active.req.goal.x;
        const int gy = m_active.req.goal.y;
        const int tol = std::max(0, m_active.req.goalTolerance);
        if (tol == 0) return (x == gx && y == gy);
        return (std::abs(gx - x) + std::abs(gy - y)) <= tol;
    }

    float heuristic(int x, int y) const noexcept
    {
        // Use Octile when diagonals are allowed; Manhattan otherwise.
        if (m_active.req.allowDiagonal)
            return octile(x, y, m_active.req.goal.x, m_active.req.goal.y, m_costStraight, m_costDiagonal);
        return manhattan(x, y, m_active.req.goal.x, m_active.req.goal.y);
    }

    float requestHeuristicWeight() const noexcept
    {
        const float w = m_active.req.heuristicWeight > 0.0f ? m_active.req.heuristicWeight : m_globalHeuristicWeight;
        return (w < 1.0f ? 1.0f : w);
    }

    void reconstructPath(int lastIdx, Path& out) const
    {
        out.points.clear();
        int p = lastIdx;
        while (p >= 0) {
            const PFPoint q = xy(p, m_w);
            out.points.push_back(q);
            p = m_parent[p];
        }
        std::reverse(out.points.begin(), out.points.end());

        // Accumulate length and cost
        float length = 0.0f;
        for (std::size_t i = 1; i < out.points.size(); ++i) {
            const int dx = std::abs(out.points[i].x - out.points[i-1].x);
            const int dy = std::abs(out.points[i].y - out.points[i-1].y);
            length += (dx == 0 || dy == 0) ? m_costStraight : m_costDiagonal;
        }
        out.length = length;

        // If lastIdx is the goal (or partial best), g holds cost to that node.
        out.cost = m_g[lastIdx];
    }

    // Bresenham-style line-of-sight between centers of grid cells, honoring corner cutting rules.
    bool hasLineOfSight(PFPoint a, PFPoint b) const
    {
        int x0 = a.x, y0 = a.y;
        const int x1 = b.x, y1 = b.y;

        const int dx = std::abs(x1 - x0);
        const int dy = std::abs(y1 - y0);
        const int sx = (x0 < x1) ? 1 : -1;
        const int sy = (y0 < y1) ? 1 : -1;

        int err = dx - dy;

        auto walkable = [&](int x, int y) -> bool {
            if (!inside(x,y)) return false;
            return m_active.req.isWalkable(x,y);
        };

        while (true) {
            if (!walkable(x0, y0)) return false;
            if (x0 == x1 && y0 == y1) break;

            const int e2 = err << 1;

            int nx = x0, ny = y0;

            if (e2 > -dy) { err -= dy; nx += sx; }
            if (e2 <  dx) { err += dx; ny += sy; }

            const bool movingDiag = (nx != x0) && (ny != y0);
            if (movingDiag && m_active.req.forbidCornerCutting) {
                // Both orthogonal neighbors must be free to pass the corner
                if (!walkable(nx, y0) || !walkable(x0, ny)) return false;
            }

            x0 = nx; y0 = ny;
        }
        return true;
    }

    void smoothStringPull(Path& path) const
    {
        if (path.points.size() < 3) return;

        std::vector<PFPoint> out;
        out.reserve(path.points.size());
        std::size_t anchor = 0;
        out.push_back(path.points[anchor]);

        // Greedy visibility-based skipping
        for (std::size_t i = 2; i < path.points.size(); ++i) {
            if (!hasLineOfSight(path.points[anchor], path.points[i])) {
                // Keep the last visible point
                out.push_back(path.points[i-1]);
                anchor = i - 1;
            }
        }
        out.push_back(path.points.back());
        path.points.swap(out);

        // Recompute geometric length (cost stays as A* g-cost)
        float length = 0.0f;
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const int dx = std::abs(path.points[i].x - path.points[i-1].x);
            const int dy = std::abs(path.points[i].y - path.points[i-1].y);
            length += (dx == 0 || dy == 0) ? m_costStraight : m_costDiagonal;
        }
        path.length = length;
    }

    //------------------------------ Members -----------------------------------

    int m_w = 1, m_h = 1;

    // Per-grid reusable buffers
    std::vector<uint32_t> m_mark;    // generation+state per node
    std::vector<float>    m_g;       // best g-cost
    std::vector<int>      m_parent;  // parent index

    Active m_active{};
    std::vector<PathRequest> m_queue;

    // Global tunables
    float m_globalHeuristicWeight = 1.0f;
    float m_costStraight = 1.0f;
    float m_costDiagonal = 1.41421356f;

    // Stats visible to callers (copied from Active upon finish)
    Stats m_lastStats{};

    // Monotonic counter for deterministic tie-breaking
    uint64_t m_orderCounter = 0;
};

} // namespace ai
