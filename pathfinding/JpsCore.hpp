// pathfinding/JpsCore.hpp
#pragma once
#include "pathfinding/Jps.hpp"

#include <queue>
#include <array>
#include <vector>
#include <limits>
#include <utility>
#include <cmath>
#include <algorithm>

namespace colony::path::detail {

// -------------------- Internal graph/search types ----------------------------

struct Node {
    int   x = 0, y = 0;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    int   parent = -1;     // parent index in flat grid (y*W + x)
    int   px = 0, py = 0;  // parent coordinates (for direction)
    bool  opened = false;
    bool  closed = false;
};

struct PQItem {
    int index;   // y*W + x
    float f;
    // min-heap by f:
    bool operator<(const PQItem& o) const { return f > o.f; }
};

inline int idx(int x, int y, int W) { return y * W + x; }

// -------------------- Grid / movement helpers --------------------------------

inline bool in_bounds(const IGrid& g, int x, int y) {
    return (x >= 0 && y >= 0 && x < g.width() && y < g.height());
}

inline bool passable(const IGrid& g, int x, int y) {
    return in_bounds(g, x, y) && g.walkable(x, y);
}

inline bool can_step(const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o) {
    const int nx = x + dx, ny = y + dy;
    if (!passable(g, nx, ny)) return false;
    // forbid corner cutting if requested
    if (o.allowDiagonal && o.dontCrossCorners && dx != 0 && dy != 0) {
        if (!passable(g, x + dx, y) || !passable(g, x, y + dy)) return false;
    }
    return true;
}

// -------------------- Metric / cost ------------------------------------------

inline float heuristic(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    const int dx = std::abs(x0 - x1);
    const int dy = std::abs(y0 - y1);
    if (!o.allowDiagonal) return static_cast<float>(dx + dy) * o.costStraight; // Manhattan
    const int dmin = std::min(dx, dy);
    const int dmax = std::max(dx, dy);
    return dmin * o.costDiagonal + (dmax - dmin) * o.costStraight;             // Octile
}

inline float dist_cost(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    const int dx = std::abs(x0 - x1);
    const int dy = std::abs(y0 - y1);
    const int dmin = std::min(dx, dy);
    const int dmax = std::max(dx, dy);
    return dmin * o.costDiagonal + (dmax - dmin) * o.costStraight;
}

// tiny tie-breaker to prefer straighter paths (keeps w=1 optimal)
inline float tiebreak(int x, int y, int sx, int sy, int gx, int gy) {
    const float vx1 = static_cast<float>(x - gx), vy1 = static_cast<float>(y - gy);
    const float vx2 = static_cast<float>(sx - gx), vy2 = static_cast<float>(sy - gy);
    return std::abs(vx1 * vy2 - vy1 * vx2) * 1e-3f;
}

// -------------------- Forced neighbors ---------------------------------------

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
    // diagonal forced-neighbor patterns
    if (!passable(g, x - dx, y) && passable(g, x - dx, y + dy)) return true;
    if (!passable(g, x, y - dy) && passable(g, x + dx, y - dy)) return true;
    return false;
}

// -------------------- Direction pruning (JPS) --------------------------------

inline void pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                        const JpsOptions& o, std::vector<std::pair<int,int>>& out)
{
    out.clear();

    // no parent -> all legal directions
    if (px == x && py == y) {
        static const std::array<std::pair<int,int>,8> dirs8{{
            {+1,0},{-1,0},{0,+1},{0,-1},{+1,+1},{+1,-1},{-1,+1},{-1,-1}
        }};
        static const std::array<std::pair<int,int>,4> dirs4{{
            {+1,0},{-1,0},{0,+1},{0,-1}
        }};
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
        // forced neighbors
        if (!passable(g,x - dx,y) && can_step(g,x,y,-dx,dy,o)) out.emplace_back(-dx,dy);
        if (!passable(g,x,y - dy) && can_step(g,x,y, dx,-dy,o)) out.emplace_back( dx,-dy);
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

// -------------------- Core jump ----------------------------------------------

inline bool jump(const IGrid& g, int x, int y, int dx, int dy,
                 int gx, int gy, const JpsOptions& o, int& outx, int& outy)
{
    while (true) {
        if (!can_step(g, x, y, dx, dy, o)) return false;
        x += dx; y += dy;

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

// -------------------- LOS smoothing (supercover) -----------------------------

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
        const int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }

        if (o.allowDiagonal && o.dontCrossCorners) {
            if ((x != ex) && (y != ey)) {
                if (!tile_ok(ex, y) || !tile_ok(x, ey)) return false;
            }
        }
    }
}

// -------------------- Path reconstruction ------------------------------------

inline std::vector<Cell> reconstruct_path(const std::vector<Node>& nodes, int i, int W) {
    std::vector<Cell> path;
    while (i != -1) {
        const int x = i % W, y = i / W;
        path.push_back({x,y});
        i = nodes[i].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace colony::path::detail
