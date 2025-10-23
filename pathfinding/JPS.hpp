// pathfinding/jps.hpp
// Minimal, production-friendly Jump Point Search for uniform-cost grids.
// MSVC /std:c++20; header-only for easy drop-in.

#pragma once
#include <vector>
#include <queue>
#include <array>
#include <limits>
#include <optional>
#include <cstdint>
#include <cmath>
#include <functional>

namespace colony::pf {

struct Point { int x{}, y{}; };

struct JpsParams {
    bool allow_diagonal = true;
    bool forbid_corner_cutting = true;  // typical JPS assumption
    int  D  = 10;  // straight step cost (use 1 if you like)
    int  D2 = 14;  // diagonal step cost (≈ D*sqrt(2) without FP)
};

// Lightweight grid view: user supplies passability and bounds.
struct GridView {
    int w{}, h{};
    // Return true iff (x,y) is inside and traversable
    std::function<bool(int,int)> passable;

    bool in_bounds(int x, int y) const noexcept {
        return static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
               static_cast<unsigned>(y) < static_cast<unsigned>(h);
    }
    // Treat OOB as blocked (helps forced-neighbor tests)
    bool walkable(int x, int y) const noexcept {
        return in_bounds(x,y) && passable(x,y);
    }
};

// Heuristic: Octile (diagonal) for 8-connected grids
inline int h_octile(const Point& a, const Point& b, const JpsParams& p) {
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    int m = std::min(dx, dy);
    return p.D * (dx + dy) + (p.D2 - 2 * p.D) * m;
}

struct NodeRec {
    Point p{};
    int g = std::numeric_limits<int>::max();
    int f = std::numeric_limits<int>::max();
    int parent_idx = -1;
    int dir = 8; // 0..7 = incoming direction, 8 = none(start)
};

struct Path {
    std::vector<Point> points;
    int total_cost = 0;
};

// 8 directions, clockwise starting East (any consistent order is fine)
static constexpr std::array<int, 16> DIRS = {
    1,0,  1,-1,  0,-1,  -1,-1,  -1,0,  -1,1,  0,1,  1,1
};
inline int dx_of(int dir) { return DIRS[2*dir+0]; }
inline int dy_of(int dir) { return DIRS[2*dir+1]; }

// Corner-cutting rule for diagonal step (x,y) -> (x+dx,y+dy).
inline bool can_step_diag(const GridView& g, int x, int y, int dx, int dy, const JpsParams& p) {
    if (!p.allow_diagonal) return false;
    if (p.forbid_corner_cutting) {
        return g.walkable(x + dx, y) && g.walkable(x, y + dy) && g.walkable(x + dx, y + dy);
    }
    // If corner cutting allowed, just require destination be walkable.
    return g.walkable(x + dx, y + dy);
}

// Forced-neighbor detection (classic JPS)
inline bool has_forced_straight(const GridView& g, int x, int y, int dx, int dy) {
    // Horizontal
    if (dy == 0 && dx != 0) {
        // up blocked & up-right free OR down blocked & down-right free (mirror for dx<0)
        int sx = (dx > 0 ? 1 : -1);
        return ( !g.walkable(x, y-1) && g.walkable(x+sx, y-1) ) ||
               ( !g.walkable(x, y+1) && g.walkable(x+sx, y+1) );
    }
    // Vertical
    if (dx == 0 && dy != 0) {
        int sy = (dy > 0 ? 1 : -1);
        return ( !g.walkable(x-1, y) && g.walkable(x-1, y+sy) ) ||
               ( !g.walkable(x+1, y) && g.walkable(x+1, y+sy) );
    }
    return false;
}

// When moving diagonally, classic JPS checks for orthogonal block/free patterns.
inline bool has_forced_diag(const GridView& g, int x, int y, int dx, int dy) {
    // Example NE (dx=+1,dy=-1): if West blocked & NW free OR South blocked & SE free
    int sx = (dx > 0 ? -1 : 1);
    int sy = (dy > 0 ? -1 : 1);
    return ( !g.walkable(x + sx, y)   && g.walkable(x + sx, y + dy) ) ||
           ( !g.walkable(x,     y + sy) && g.walkable(x + dx, y + sy) );
}

// Produce pruned neighbor directions given the incoming direction.
// Start node (dir==8): consider all legal directions from there.
inline void pruned_neighbor_dirs(const GridView& g, const Point& p, int inDir,
                                 const JpsParams& prm, std::array<int,8>& out, int& outN)
{
    outN = 0;
    auto try_dir = [&](int dir) {
        int dx = dx_of(dir), dy = dy_of(dir);
        if (dy == 0 && dx != 0) {
            if (g.walkable(p.x + dx, p.y)) out[outN++] = dir;
        } else if (dx == 0 && dy != 0) {
            if (g.walkable(p.x, p.y + dy)) out[outN++] = dir;
        } else { // diagonal
            if (can_step_diag(g, p.x, p.y, dx, dy, prm)) out[outN++] = dir;
        }
    };

    if (inDir == 8) {
        // Start: all natural directions
        for (int d = 0; d < 8; ++d) try_dir(d);
        return;
    }

    int dx = dx_of(inDir), dy = dy_of(inDir);

    if (dy == 0 && dx != 0) {
        // natural: straight forward; forced: diagonals if blocking causes them
        try_dir(inDir);
        // Up-left/right and down-left/right are forced if their orthogonal behind is blocked.
        int upDiag    = (dx > 0) ? 1 /*NE*/ : 3 /*NW*/;
        int downDiag  = (dx > 0) ? 7 /*SE*/ : 5 /*SW*/;
        if (!g.walkable(p.x, p.y-1) && g.walkable(p.x+dx, p.y-1)) try_dir(upDiag);
        if (!g.walkable(p.x, p.y+1) && g.walkable(p.x+dx, p.y+1)) try_dir(downDiag);
    } else if (dx == 0 && dy != 0) {
        try_dir(inDir);
        int leftDiag  = (dy > 0) ? 5 /*SW*/ : 3 /*NW*/;
        int rightDiag = (dy > 0) ? 7 /*SE*/ : 1 /*NE*/;
        if (!g.walkable(p.x-1, p.y) && g.walkable(p.x-1, p.y+dy)) try_dir(leftDiag);
        if (!g.walkable(p.x+1, p.y) && g.walkable(p.x+1, p.y+dy)) try_dir(rightDiag);
    } else {
        // diagonal motion: natural neighbors are (diag, horiz, vert)
        try_dir(inDir);
        try_dir( (dx > 0) ? 0 : 4 );  // horizontal component
        try_dir( (dy > 0) ? 6 : 2 );  // vertical component

        // Optional: add extra forced diagonals if obstacles create them.
        // Classic JPS will discover these via jump() tests below; explicit here is fine.
    }
}

// Jump in (dx,dy) until hit obstacle, forced neighbor, or goal; return jump point.
inline std::optional<Point> jump(const GridView& g, const Point& start, int dx, int dy,
                                 const Point& goal, const JpsParams& prm)
{
    int x = start.x + dx, y = start.y + dy;

    // Step feasibility for first move:
    if (dy == 0 && dx != 0) {
        if (!g.walkable(x, y)) return std::nullopt;
    } else if (dx == 0 && dy != 0) {
        if (!g.walkable(x, y)) return std::nullopt;
    } else {
        if (!can_step_diag(g, start.x, start.y, dx, dy, prm)) return std::nullopt;
        if (!g.walkable(x, y)) return std::nullopt;
    }

    // Iterate forward
    for (;; x += dx, y += dy)
    {
        Point cur{x,y};
        if (cur.x == goal.x && cur.y == goal.y) return cur;

        if (dy == 0 && dx != 0) {
            if (has_forced_straight(g, x, y, dx, dy)) return cur;
        } else if (dx == 0 && dy != 0) {
            if (has_forced_straight(g, x, y, dx, dy)) return cur;
        } else {
            // Diagonal: if either straight recursion finds a jump, current is a jump point.
            if (auto j = jump(g, cur, dx, 0, goal, prm)) return cur;
            if (auto j = jump(g, cur, 0, dy, goal, prm)) return cur;
            if (has_forced_diag(g, x, y, dx, dy)) return cur;
            // Corner cutting guard for *next* diagonal step
            if (!can_step_diag(g, x, y, dx, dy, prm)) return std::nullopt;
        }

        // Next cell must be walkable in straight motion
        if ((dy == 0 && dx != 0) || (dx == 0 && dy != 0)) {
            if (!g.walkable(x + dx, y + dy)) return std::nullopt;
        }
    }
}

// A* + JPS successor generator
struct OpenCmp {
    bool operator()(const int a, const int b) const { return nodes[a].f > nodes[b].f; }
    static std::vector<NodeRec> nodes; // bound at runtime
};
// static storage for comparator
inline std::vector<NodeRec> OpenCmp::nodes;

Path FindPathJPS(const GridView& grid, const Point& start, const Point& goal, const JpsParams& prm)
{
    if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) return {};

    // Pre-size node grid (flattened)
    const int N = grid.w * grid.h;
    OpenCmp::nodes.assign(N, {});
    auto idx = [&](int x,int y){ return y*grid.w + x; };

    auto push_or_update = [&](int x, int y, int g, int parent_idx, int inDir,
                              auto& open, const Point& goalPt, const JpsParams& p)
    {
        int i = idx(x,y);
        NodeRec& n = OpenCmp::nodes[i];
        if (n.f == std::numeric_limits<int>::max() || g < n.g) {
            n.p = {x,y};
            n.g = g;
            n.f = g + h_octile(n.p, goalPt, p);
            n.parent_idx = parent_idx;
            n.dir = inDir;
            open.push(i);
        }
    };

    // Init
    std::priority_queue<int, std::vector<int>, OpenCmp> open;
    std::vector<uint8_t> closed(N, 0);

    int s = idx(start.x, start.y);
    OpenCmp::nodes[s] = {start, 0, h_octile(start, goal, prm), -1, 8};
    open.push(s);

    // A*
    while (!open.empty()) {
        int ci = open.top(); open.pop();
        NodeRec cur = OpenCmp::nodes[ci];
        if (closed[ci]) continue;
        closed[ci] = 1;

        if (cur.p.x == goal.x && cur.p.y == goal.y) {
            // Reconstruct
            Path path; path.total_cost = cur.g;
            for (int i = ci; i != -1; i = OpenCmp::nodes[i].parent_idx)
                path.points.push_back(OpenCmp::nodes[i].p);
            std::reverse(path.points.begin(), path.points.end());
            return path;
        }

        // Generate pruned neighbor directions
        std::array<int,8> dirs{};
        int dirN = 0;
        pruned_neighbor_dirs(grid, cur.p, cur.dir, prm, dirs, dirN);

        // For each direction, JUMP and enqueue jump point (if any)
        for (int k=0; k<dirN; ++k) {
            int d = dirs[k], dx = dx_of(d), dy = dy_of(d);
            if (auto jp = jump(grid, cur.p, dx, dy, goal, prm)) {
                // cost to jump point = straight/diag steps * D/D2
                int steps = std::max(std::abs(jp->x - cur.p.x), std::abs(jp->y - cur.p.y));
                int diag = std::min(std::abs(jp->x - cur.p.x), std::abs(jp->y - cur.p.y));
                int straight = steps - diag;
                int cost = cur.g + diag*prm.D2 + straight*prm.D;
                push_or_update(jp->x, jp->y, cost, ci, d, open, goal, prm);
            }
        }
    }
    return {}; // no path
}

} // namespace colony::pf
