// pathfinding/Jps.cpp
// Include the public API FIRST so helper definitions can call IGrid methods:
#include <pathfinding/Jps.hpp>  // defines IGrid, Cell, JpsOptions, jps_find_path
#include "JpsCore.hpp"          // helper declarations + Node/PQItem

#include <queue>
#include <vector>
#include <limits>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace colony::path {
namespace detail {

// ========= Windows-only lightweight timing (opt-in) =========
#ifdef COLONY_PF_TIMERS
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct JpsTimers {
    long long pop_ns    = 0;
    long long jump_ns   = 0;
    long long smooth_ns = 0;
    int        pops     = 0;
    int        jumps    = 0;
};
static thread_local JpsTimers g_jps_timers;

struct Qpc {
    LARGE_INTEGER freq{};
    Qpc() { QueryPerformanceFrequency(&freq); }
    long long to_ns(LONGLONG ticks) const {
        return (ticks * 1000000000LL) / freq.QuadPart;
    }
    static const Qpc& instance() { static Qpc q; return q; }
};

struct ScopedQpc {
    long long& acc;
    LARGE_INTEGER start{};
    explicit ScopedQpc(long long& dst) : acc(dst) { QueryPerformanceCounter(&start); }
    ~ScopedQpc() {
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        acc += Qpc::instance().to_ns(end.QuadPart - start.QuadPart);
    }
};
#endif // COLONY_PF_TIMERS

// ========= Small utilities (private) =========

inline int idx(int x, int y, int W) { return y * W + x; }

inline bool in_bounds(const IGrid& g, int x, int y) {
    return (x >= 0 && y >= 0 && x < g.width() && y < g.height());
}

inline bool passable(const IGrid& g, int x, int y) {
    return in_bounds(g, x, y) && g.walkable(x, y);
}

inline bool can_step(const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o) {
    const int nx = x + dx, ny = y + dy;
    if (!passable(g, nx, ny)) return false;
    if (o.allowDiagonal && o.dontCrossCorners && dx != 0 && dy != 0) {
        // prevent squeezing through blocked corners
        if (!passable(g, x + dx, y) || !passable(g, x, y + dy)) return false;
    }
    return true;
}

// Octile (diagonals) or Manhattan (orthogonal) heuristic for grids.
inline float heuristic(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    const int dx = std::abs(x0 - x1);
    const int dy = std::abs(y0 - y1);
    if (!o.allowDiagonal) return static_cast<float>(dx + dy) * o.costStraight;
    const int dmin = std::min(dx, dy);
    const int dmax = std::max(dx, dy);
    return static_cast<float>(dmin) * o.costDiagonal
         + static_cast<float>(dmax - dmin) * o.costStraight;
}

inline float dist_cost(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    const int dx = std::abs(x0 - x1);
    const int dy = std::abs(y0 - y1);
    const int dmin = std::min(dx, dy);
    const int dmax = std::max(dx, dy);
    return static_cast<float>(dmin) * o.costDiagonal
         + static_cast<float>(dmax - dmin) * o.costStraight;
}

// Slight tie‑breaker so straighter paths win ties.
inline float tiebreak(int x, int y, int sx, int sy, int gx, int gy) {
    const float vx1 = static_cast<float>(x - gx);
    const float vy1 = static_cast<float>(y - gy);
    const float vx2 = static_cast<float>(sx - gx);
    const float vy2 = static_cast<float>(sy - gy);
    return std::abs(vx1 * vy2 - vy1 * vx2) * 1e-3f;
}

inline bool has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy) {
    if (dx != 0 && dy == 0) { // horizontal
        if (!passable(g, x, y + 1) && passable(g, x + dx, y + 1)) return true;
        if (!passable(g, x, y - 1) && passable(g, x + dx, y - 1)) return true;
    } else if (dx == 0 && dy != 0) { // vertical
        if (!passable(g, x + 1, y) && passable(g, x + 1, y + dy)) return true;
        if (!passable(g, x - 1, y) && passable(g, x - 1, y + dy)) return true;
    }
    return false;
}

inline bool has_forced_neighbors_diag(const IGrid& g, int x, int y, int dx, int dy) {
    // diagonal patterns
    if (!passable(g, x - dx, y) && passable(g, x - dx, y + dy)) return true;
    if (!passable(g, x, y - dy) && passable(g, x + dx, y - dy)) return true;
    return false;
}

inline void pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                        const JpsOptions& o, std::vector<std::pair<int,int>>& out)
{
    out.clear();
    if (px == x && py == y) { // no parent: expose all legal directions
        static constexpr std::pair<int,int> dirs8[] {
            {+1,0}, {-1,0}, {0,+1}, {0,-1},
            {+1,+1}, {+1,-1}, {-1,+1}, {-1,-1}
        };
        static constexpr std::pair<int,int> dirs4[] {
            {+1,0}, {-1,0}, {0,+1}, {0,-1}
        };
        if (o.allowDiagonal) {
            for (auto [dx,dy] : dirs8) if (can_step(g,x,y,dx,dy,o)) out.emplace_back(dx,dy);
        } else {
            for (auto [dx,dy] : dirs4) if (can_step(g,x,y,dx,dy,o)) out.emplace_back(dx,dy);
        }
        return;
    }

    int dx = (x - px); dx = (dx>0) ? 1 : (dx<0 ? -1 : 0);
    int dy = (y - py); dy = (dy>0) ? 1 : (dy<0 ? -1 : 0);

    if (dx != 0 && dy != 0) {
        // natural neighbors
        if (can_step(g,x,y,dx,dy,o)) out.emplace_back(dx,dy);
        if (can_step(g,x,y,dx,0,o))  out.emplace_back(dx,0);
        if (can_step(g,x,y,0,dy,o))  out.emplace_back(0,dy);
        // forced
        if (!passable(g,x - dx,y)   && can_step(g,x,y,-dx,dy,o)) out.emplace_back(-dx,dy);
        if (!passable(g,x,y - dy)   && can_step(g,x,y, dx,-dy,o)) out.emplace_back( dx,-dy);
    } else if (dx != 0) {
        if (can_step(g,x,y,dx,0,o)) out.emplace_back(dx,0);
        if (!passable(g,x,y+1) && can_step(g,x,y,dx,+1,o)) out.emplace_back(dx,+1);
        if (!passable(g,x,y-1) && can_step(g,x,y,dx,-1,o)) out.emplace_back(dx,-1);
    } else { // dy != 0
        if (can_step(g,x,y,0,dy,o)) out.emplace_back(0,dy);
        if (!passable(g,x+1,y) && can_step(g,x,y,+1,dy,o)) out.emplace_back(+1,dy);
        if (!passable(g,x-1,y) && can_step(g,x,y,-1,dy,o)) out.emplace_back(-1,dy);
    }
}

inline bool jump(const IGrid& g, int x, int y, int dx, int dy,
                 int gx, int gy, const JpsOptions& o, int& outx, int& outy)
{
#ifdef COLONY_PF_TIMERS
    ScopedQpc _jt(g_jps_timers.jump_ns);
    ++g_jps_timers.jumps;
#endif
    while (true) {
        const int nx = x + dx, ny = y + dy;
        if (!can_step(g, x, y, dx, dy, o)) return false;
        x = nx; y = ny;

        if (x == gx && y == gy) { outx = x; outy = y; return true; }

        if (dx != 0 && dy != 0) {
            if (has_forced_neighbors_diag(g, x, y, dx, dy)) { outx = x; outy = y; return true; }
            int jx = 0, jy = 0;
            if (jump(g, x, y, dx, 0, gx, gy, o, jx, jy)) { outx = x; outy = y; return true; }
            if (jump(g, x, y, 0, dy, gx, gy, o, jx, jy)) { outx = x; outy = y; return true; }
        } else {
            if (has_forced_neighbors_straight(g, x, y, dx, dy)) { outx = x; outy = y; return true; }
        }
    }
}

// LOS "supercover" — checks every touched cell (great for path smoothing).
inline bool los_supercover(const IGrid& g, int x0, int y0, int x1, int y1, const JpsOptions& o)
{
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int x = x0, y = y0;
    auto tile_ok = [&](int tx, int ty) -> bool { return passable(g, tx, ty); };

    while (true) {
        if (!tile_ok(x,y)) return false;
        if (x == x1 && y == y1) return true;

        const int ex = x, ey = y;
        const int e2 = err * 2; // avoid left-shifting negative values
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }

        if (o.allowDiagonal && o.dontCrossCorners) {
            if ((x != ex) && (y != ey)) {
                if (!tile_ok(ex, y) || !tile_ok(x, ey))
                    return false;
            }
        }
    }
}

static std::vector<Cell> reconstruct_path(const std::vector<Node>& nodes, int i, int W) {
    std::vector<Cell> path;
    while (i != -1) {
        const int x = i % W, y = i / W;
        path.push_back(Cell{x, y});
        i = nodes[static_cast<std::size_t>(i)].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace detail

// ========= Public entry: JPS (A* + jump pruning) =========
// Harabor & Grastien 2011/2014. :contentReference[oaicite:2]{index=2}
std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
{
    using namespace detail;

    const int W = grid.width();
    const int H = grid.height(); (void)H;

#ifndef NDEBUG
    // For strict optimality guarantees (classic A* / JPS), the heuristic must be
    // admissible/consistent. A weight w>1 (Weighted A*) trades optimality for speed.
    const float w = opt.heuristicWeight;
    assert(std::abs(w - 1.0f) < 1e-6f &&
           "heuristicWeight must be 1.0 for JPS/A* optimality (w>1 => bounded-suboptimal)");
#endif

    if (W <= 0) return {};
    if (!passable(grid, start.x, start.y)) return {};
    if (!passable(grid, goal.x, goal.y))   return {};
    if (start.x == goal.x && start.y == goal.y) return std::vector<Cell>{start};

    std::vector<Node> nodes(static_cast<std::size_t>(W) * static_cast<std::size_t>(grid.height()));

    auto push_open = [&](std::priority_queue<PQItem>& open, int i, float f) {
        open.push(PQItem{i, f});
        nodes[static_cast<std::size_t>(i)].opened = true;
    };

    // init start
    const int sidx = idx(start.x, start.y, W);
    Node& s = nodes[static_cast<std::size_t>(sidx)];
    s.x = start.x; s.y = start.y;
    s.g = 0.0f;
    s.f = opt.heuristicWeight * heuristic(start.x, start.y, goal.x, goal.y, opt);
    s.parent = -1;
    s.px = start.x; s.py = start.y;

    std::priority_queue<PQItem> open;
    push_open(open, sidx, s.f);

    std::vector<std::pair<int,int>> dirs;
    dirs.reserve(8);

    while (!open.empty()) {
#ifdef COLONY_PF_TIMERS
        LARGE_INTEGER _t0, _t1; QueryPerformanceCounter(&_t0);
#endif
        const int curr_i = open.top().index; open.pop();
#ifdef COLONY_PF_TIMERS
        QueryPerformanceCounter(&_t1);
        g_jps_timers.pop_ns += Qpc::instance().to_ns(_t1.QuadPart - _t0.QuadPart);
        ++g_jps_timers.pops;
#endif

        Node& n = nodes[static_cast<std::size_t>(curr_i)];
        if (n.closed) continue;
        n.closed = true;

        if (curr_i == idx(goal.x, goal.y, W)) {
            auto path = reconstruct_path(nodes, curr_i, W);
            if (opt.smoothPath && path.size() > 2) {
#ifdef COLONY_PF_TIMERS
                ScopedQpc _st(g_jps_timers.smooth_ns);
#endif
                // greedily pull strings with LOS supercover
                std::vector<Cell> smooth; smooth.push_back(path.front());
                std::size_t j = 1;
                while (j < path.size()) {
                    std::size_t k = j;
                    while (k+1 < path.size() &&
                           los_supercover(grid, smooth.back().x, smooth.back().y, path[k+1].x, path[k+1].y, opt)) {
                        ++k;
                    }
                    smooth.push_back(path[k]);
                    j = k + 1;
                }
                return smooth;
            }
            return path;
        }

        // Expand with JPS
        const int cx = n.x, cy = n.y;
        pruned_dirs(grid, cx, cy, n.px, n.py, opt, dirs);

        for (auto [dx,dy] : dirs) {
            int jx = 0, jy = 0;
            if (!jump(grid, cx, cy, dx, dy, goal.x, goal.y, opt, jx, jy))
                continue;

            const int ji = idx(jx, jy, W);
            const float tentative_g = n.g + dist_cost(cx, cy, jx, jy, opt);

            Node& m = nodes[static_cast<std::size_t>(ji)];
            if (!m.opened || tentative_g < m.g) {
                m.x = jx; m.y = jy;
                m.g = tentative_g;
                const float h = heuristic(jx, jy, goal.x, goal.y, opt) * opt.heuristicWeight;
                const float f = tentative_g + h
                              + (opt.tieBreakCross
                                 ? tiebreak(jx, jy, start.x, start.y, goal.x, goal.y)
                                 : 0.0f);
                m.f = f;
                m.parent = curr_i;
                m.px = cx; m.py = cy;
                push_open(open, ji, f);
            }
        }
    }
    return {}; // no path
}

} // namespace colony::path
