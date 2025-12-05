#include "pathfinding/Jps.hpp"
#include <queue>
#include <array>
#include <vector>
#include <limits>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace colony::path {

namespace {
struct Node {
    int x, y;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    int parent = -1;     // parent index in flat grid (y*W+x)
    int px = 0, py = 0;  // parent coordinates (for direction)
    bool opened = false;
    bool closed = false;
};

struct PQItem {
    int index;   // y*W + x
    float f;
    bool operator<(const PQItem& o) const { return f > o.f; } // min-heap
};

inline int idx(int x, int y, int W) { return y * W + x; }

inline bool in_bounds(const IGrid& g, int x, int y) {
    return (x >= 0 && y >= 0 && x < g.width() && y < g.height());
}

inline bool passable(const IGrid& g, int x, int y) {
    return in_bounds(g, x, y) && g.walkable(x, y);
}

inline bool can_step(const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o) {
    int nx = x + dx, ny = y + dy;
    if (!passable(g, nx, ny)) return false;
    if (o.allowDiagonal && o.dontCrossCorners && dx != 0 && dy != 0) {
        // forbid cutting across corners
        if (!passable(g, x + dx, y) || !passable(g, x, y + dy)) return false;
    }
    return true;
}

// Octile distance for 8-connected grids (Manhattan when diagonals disabled)
// Reference: octile distance for grids with diag cost sqrt(2) and straight cost 1. :contentReference[oaicite:1]{index=1}
inline float heuristic(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    int dx = std::abs(x0 - x1);
    int dy = std::abs(y0 - y1);
    if (!o.allowDiagonal) return static_cast<float>(dx + dy) * o.costStraight;
    int dmin = std::min(dx, dy);
    int dmax = std::max(dx, dy);
    return dmin * o.costDiagonal + (dmax - dmin) * o.costStraight;
}

inline float dist_cost(int x0, int y0, int x1, int y1, const JpsOptions& o) {
    int dx = std::abs(x0 - x1);
    int dy = std::abs(y0 - y1);
    int dmin = std::min(dx, dy);
    int dmax = std::max(dx, dy);
    return dmin * o.costDiagonal + (dmax - dmin) * o.costStraight;
}

// Slight tie-breaker to prefer straighter paths (does not break optimality for w=1)
inline float tiebreak(int x, int y, int sx, int sy, int gx, int gy) {
    // cross product magnitude between (current->goal) and (start->goal)
    float vx1 = static_cast<float>(x - gx), vy1 = static_cast<float>(y - gy);
    float vx2 = static_cast<float>(sx - gx), vy2 = static_cast<float>(sy - gy);
    return std::abs(vx1 * vy2 - vy1 * vx2) * 1e-3f;
}

// Forced-neighbor tests per JPS (Harabor & Grastien 2011). :contentReference[oaicite:2]{index=2}
inline bool has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy) {
    if (dx != 0 && dy == 0) {
        // moving horizontally
        // if above is blocked and up-ahead is free, or below blocked and down-ahead is free
        if (!passable(g, x, y + 1) && passable(g, x + dx, y + 1)) return true;
        if (!passable(g, x, y - 1) && passable(g, x + dx, y - 1)) return true;
    } else if (dx == 0 && dy != 0) {
        // moving vertically
        if (!passable(g, x + 1, y) && passable(g, x + 1, y + dy)) return true;
        if (!passable(g, x - 1, y) && passable(g, x - 1, y + dy)) return true;
    }
    return false;
}

inline bool has_forced_neighbors_diag(const IGrid& g, int x, int y, int dx, int dy) {
    // diagonal: check the two forced-neighbor patterns
    if (!passable(g, x - dx, y) && passable(g, x - dx, y + dy)) return true;
    if (!passable(g, x, y - dy) && passable(g, x + dx, y - dy)) return true;
    return false;
}

// Enumerate pruned directions from current node given the parent direction.
// Based on JPS neighbor-pruning rules (online symmetry pruning). :contentReference[oaicite:3]{index=3}
static void pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                        const JpsOptions& o, std::vector<std::pair<int,int>>& out)
{
    out.clear();
    if (px == x && py == y) { // no parent: return all legal dirs
        static const std::array<std::pair<int,int>,8> dirs8 {{
            {+1,0},{-1,0},{0,+1},{0,-1},{+1,+1},{+1,-1},{-1,+1},{-1,-1}
        }};
        static const std::array<std::pair<int,int>,4> dirs4 {{
            {+1,0},{-1,0},{0,+1},{0,-1}
        }};
        if (o.allowDiagonal) {
            for (auto [dx,dy] : dirs8) if (can_step(g,x,y,dx,dy,o)) out.push_back({dx,dy});
        } else {
            for (auto [dx,dy] : dirs4) if (can_step(g,x,y,dx,dy,o)) out.push_back({dx,dy});
        }
        return;
    }

    int dx = (x - px); dx = (dx>0) ? 1 : (dx<0 ? -1 : 0);
    int dy = (y - py); dy = (dy>0) ? 1 : (dy<0 ? -1 : 0);

    if (dx != 0 && dy != 0) {
        // natural neighbors
        if (can_step(g,x,y,dx,dy,o)) out.push_back({dx,dy});
        if (can_step(g,x,y,dx,0,o))  out.push_back({dx,0});
        if (can_step(g,x,y,0,dy,o))  out.push_back({0,dy});
        // forced neighbors
        if (!passable(g,x - dx,y)   && can_step(g,x,y,-dx,dy,o)) out.push_back({-dx,dy});
        if (!passable(g,x,y - dy)   && can_step(g,x,y, dx,-dy,o)) out.push_back({ dx,-dy});
    } else if (dx != 0) {
        if (can_step(g,x,y,dx,0,o)) out.push_back({dx,0});
        if (!passable(g,x,y+1) && can_step(g,x,y,dx,+1,o)) out.push_back({dx,+1});
        if (!passable(g,x,y-1) && can_step(g,x,y,dx,-1,o)) out.push_back({dx,-1});
    } else { // dy != 0
        if (can_step(g,x,y,0,dy,o)) out.push_back({0,dy});
        if (!passable(g,x+1,y) && can_step(g,x,y,+1,dy,o)) out.push_back({+1,dy});
        if (!passable(g,x-1,y) && can_step(g,x,y,-1,dy,o)) out.push_back({-1,dy});
    }
}

// The core JPS "jump" — scans forward along (dx,dy) until it hits:
//  * the goal,
//  * a cell with a forced neighbor (i.e., a jump point), or
//  * a wall (failure).
// On diagonal moves, also stop if horizontal or vertical sub-jumps find a jump point. :contentReference[oaicite:4]{index=4}
static bool jump(const IGrid& g, int x, int y, int dx, int dy,
                 int gx, int gy, const JpsOptions& o, int& outx, int& outy)
{
    while (true) {
        int nx = x + dx, ny = y + dy;
        if (!can_step(g, x, y, dx, dy, o)) return false;
        x = nx; y = ny;

        if (x == gx && y == gy) { outx = x; outy = y; return true; }

        if (dx != 0 && dy != 0) {
            if (has_forced_neighbors_diag(g, x, y, dx, dy)) { outx = x; outy = y; return true; }
            int jx, jy;
            // Check for jump points in horizontal/vertical directions
            if (jump(g, x, y, dx, 0, gx, gy, o, jx, jy)) { outx = x; outy = y; return true; }
            if (jump(g, x, y, 0, dy, gx, gy, o, jx, jy)) { outx = x; outy = y; return true; }
        } else {
            if (has_forced_neighbors_straight(g, x, y, dx, dy)) { outx = x; outy = y; return true; }
        }
        // otherwise continue stepping
    }
}

// Optional "string pulling" smoothing using a supercover line-of-sight test
static bool los_supercover(const IGrid& g, int x0, int y0, int x1, int y1, const JpsOptions& o)
{
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy, e2;

    int x = x0, y = y0;
    auto tile_ok = [&](int tx, int ty) -> bool { return passable(g, tx, ty); };

    while (true) {
        if (!tile_ok(x,y)) return false;
        if (x == x1 && y == y1) return true;

        int ex = x, ey = y;
        e2 = err << 1;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }

        // prevent corner cutting if requested
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
        int x = i % W, y = i / W;
        path.push_back({x,y});
        i = nodes[i].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace

std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
{
    const int W = grid.width();
    const int H = grid.height();

    if (W <= 0 || H <= 0) return {};
    if (!passable(grid, start.x, start.y)) return {};
    if (!passable(grid, goal.x, goal.y))   return {};
    if (start.x == goal.x && start.y == goal.y) return {start};

    std::vector<Node> nodes(W * H);

    auto push_open = [&](std::priority_queue<PQItem>& open, int i, float f) {
        open.push(PQItem{i, f});
        nodes[i].opened = true;
    };

    // Initialize start
    const int sidx = idx(start.x, start.y, W);
    nodes[sidx].x = start.x; nodes[sidx].y = start.y;
    nodes[sidx].g = 0.0f;
    nodes[sidx].f = opt.heuristicWeight * heuristic(start.x, start.y, goal.x, goal.y, opt);
    nodes[sidx].parent = -1; nodes[sidx].px = start.x; nodes[sidx].py = start.y;

    std::priority_queue<PQItem> open;
    push_open(open, sidx, nodes[sidx].f);

    std::vector<std::pair<int,int>> dirs;
    dirs.reserve(8);

    while (!open.empty()) {
        int curr_i = open.top().index; open.pop();
        Node& n = nodes[curr_i];
        if (n.closed) continue;
        n.closed = true;

        if (curr_i == idx(goal.x, goal.y, W)) {
            auto path = reconstruct_path(nodes, curr_i, W);
            if (opt.smoothPath && path.size() > 2) {
                // greedily pull strings
                std::vector<Cell> smooth; smooth.push_back(path.front());
                size_t i = 0, j = 1;
                while (j < path.size()) {
                    // extend segment while LOS holds
                    size_t k = j;
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
        int cx = n.x, cy = n.y;
        pruned_dirs(grid, cx, cy, n.px, n.py, opt, dirs);

        for (auto [dx,dy] : dirs) {
            int jx, jy;
            if (!jump(grid, cx, cy, dx, dy, goal.x, goal.y, opt, jx, jy))
                continue;

            int ji = idx(jx, jy, W);
            float tentative_g = n.g + dist_cost(cx, cy, jx, jy, opt);

            if (!nodes[ji].opened || tentative_g < nodes[ji].g) {
                nodes[ji].x = jx; nodes[ji].y = jy;
                nodes[ji].g = tentative_g;
                float h = heuristic(jx, jy, goal.x, goal.y, opt) * opt.heuristicWeight;
                float f = tentative_g + h + (opt.tieBreakCross ? tiebreak(jx, jy, start.x, start.y, goal.x, goal.y) : 0.0f);
                nodes[ji].f = f;
                nodes[ji].parent = curr_i;
                nodes[ji].px = cx; nodes[ji].py = cy;
                push_open(open, ji, f);
            }
        }
    }

    return {}; // no path
}

} // namespace colony::path
