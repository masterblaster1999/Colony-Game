#pragma once
// JPS.hpp — Jump Point Search neighbor expander for uniform grids (Windows-friendly, header-only).
// Keeps your A* API: you still pop from OPEN, compute heuristic, etc. Only neighbor expansion changes.
//
// Requirements on Grid:
//   bool passable(int x, int y) const;   // returns false for OOB or blocked
//   bool allowDiagonal() const;          // whether 8-connected motion is allowed
//
// Usage (see section 2):
//   JPS<Grid> jps(grid, {/*allowDiagonal=*/true, /*cutCorners=*/false, /*allowSqueeze=*/false});
//   std::vector<JPS<Grid>::Successor> succ; jps.getSuccessors(curr, parentOrNull, goal, succ);
//
// References:
//   Harabor & Grastien, AAAI’11 “Online Graph Pruning for Pathfinding on Grid Maps”.
//   Harabor & Grastien, ICAPS’14 “Improving Jump Point Search”.
//   Visual explanations online (forced neighbors / jump conditions).
//   (Citations in the integration notes.)
//
// MIT-like: ship it as-is in your project.

#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>

namespace jps {

struct Coord { int x{0}, y{0}; };

struct Options {
    bool allowDiagonal = true;  // 8-connected
    bool cutCorners    = false; // if false, forbid moving diagonally when BOTH side cells are blocked
    bool allowSqueeze  = false; // if false, forbid moving diagonally when EITHER side is blocked
};

namespace detail {
    constexpr float D_STRAIGHT = 1.0f;
    constexpr float D_DIAG     = 1.41421356237f; // sqrt(2)

    inline int sgn(int v) { return (v>0) - (v<0); }

    struct Dir { int dx, dy; };
    // Moore neighborhood (8 directions)
    static constexpr std::array<Dir,8> DIRS {{
        { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    }};
}

// Template so you can adapt any grid type.
template <class Grid>
class JPS {
public:
    struct Successor { int x, y; float stepCost; };

    JPS(const Grid& g, Options opt = {}) : m_grid(g), m_opt(opt) {}

    // Parent may be nullptr for the start node.
    void getSuccessors(const Coord& cur, const Coord* parent, const Coord& goal,
                       std::vector<Successor>& out) const
    {
        out.clear();
        // 1) Get pruned directions from parent->cur
        detail::Dir dirs[8]; int nDirs = 0;
        prunedDirections(cur, parent, dirs, nDirs);

        // 2) Jump along each candidate direction; if a jump point is found, emit it
        for (int i = 0; i < nDirs; ++i) {
            Coord jp; float cost;
            if (jump(cur.x, cur.y, dirs[i].dx, dirs[i].dy, goal, jp, cost)) {
                out.push_back({jp.x, jp.y, cost});
            }
        }
    }

private:
    const Grid& m_grid;
    Options     m_opt;

    // --- Grid helpers ---
    inline bool pass(int x, int y) const {
        return m_grid.passable(x, y);
    }

    inline bool canStep(int x, int y, int dx, int dy) const {
        const int nx = x + dx, ny = y + dy;
        if (!pass(nx, ny)) return false;
        if (dx != 0 && dy != 0) {
            if (!m_opt.allowDiagonal) return false;
            const bool px = pass(x + dx, y);
            const bool py = pass(x, y + dy);
            if (!m_opt.cutCorners) {
                // No corner cutting: at least one side must be open if squeezing allowed; both if not.
                if (m_opt.allowSqueeze) {
                    if (!(px || py)) return false; // don't squeeze between two blocked cells
                } else {
                    if (!(px && py)) return false; // require both side cells free
                }
            }
        }
        return true;
    }

    // Forced neighbor predicates (Harabor & Grastien 2011)
    inline bool hasForced(int x, int y, int dx, int dy) const {
        // Moving horizontally
        if (dx != 0 && dy == 0) {
            // Blocked above but free above-forward OR blocked below but free below-forward
            if (!pass(x, y + 1) && pass(x + dx, y + 1)) return true;
            if (!pass(x, y - 1) && pass(x + dx, y - 1)) return true;
            return false;
        }
        // Moving vertically
        if (dx == 0 && dy != 0) {
            if (!pass(x + 1, y) && pass(x + 1, y + dy)) return true;
            if (!pass(x - 1, y) && pass(x - 1, y + dy)) return true;
            return false;
        }
        // Moving diagonally
        // If either side is blocked and the diagonal around it is free, we have a forced neighbor.
        if (!pass(x - dx, y) && pass(x - dx, y + dy)) return true;
        if (!pass(x, y - dy) && pass(x + dx, y - dy)) return true;
        return false;
    }

    // The JPS "jump" routine: move stepwise along (dx,dy) until goal, a forced neighbor, or blocked.
    bool jump(int x, int y, int dx, int dy, const Coord& goal, Coord& out, float& stepCost) const {
        if (!canStep(x, y, dx, dy)) return false;

        const bool diag = (dx != 0 && dy != 0);
        const float stepC = diag ? detail::D_DIAG : detail::D_STRAIGHT;

        int cx = x, cy = y;
        float acc = 0.0f;

        for (;;) {
            cx += dx; cy += dy; acc += stepC;

            // Goal reached ⇒ this is a jump point
            if (cx == goal.x && cy == goal.y) { out = {cx, cy}; stepCost = acc; return true; }

            // Forced neighbor encountered ⇒ this is a jump point
            if (hasForced(cx, cy, dx, dy)) { out = {cx, cy}; stepCost = acc; return true; }

            if (diag) {
                // Diagonal: check for jump points along the horizontal/vertical components
                Coord tmp; float tmpC;
                if (jump(cx, cy, dx, 0, goal, tmp, tmpC) || jump(cx, cy, 0, dy, goal, tmp, tmpC)) {
                    out = {cx, cy}; stepCost = acc; return true;
                }
            }

            // Keep going if we can; otherwise stop (no jump point on this ray)
            if (!canStep(cx, cy, dx, dy)) break;
        }
        return false;
    }

    // Pruned neighbor directions (natural + forced), per JPS pruning rules.
    void prunedDirections(const Coord& cur, const Coord* parent,
                          detail::Dir* out, int& count) const
    {
        count = 0;
        if (!parent) {
            // Start node: consider all valid directions from here
            for (auto d : detail::DIRS) {
                if (d.dx == 0 && d.dy == 0) continue;
                if (!m_opt.allowDiagonal && (d.dx != 0 && d.dy != 0)) continue;
                if (canStep(cur.x, cur.y, d.dx, d.dy)) out[count++] = d;
            }
            return;
        }

        const int dx = detail::sgn(cur.x - parent->x);
        const int dy = detail::sgn(cur.y - parent->y);

        // Diagonal move from parent: natural neighbors are (dx,dy), (dx,0), (0,dy)
        if (dx != 0 && dy != 0) {
            if (canStep(cur.x, cur.y, dx, dy)) out[count++] = {dx, dy};
            if (canStep(cur.x, cur.y, dx, 0))  out[count++] = {dx, 0};
            if (canStep(cur.x, cur.y, 0, dy))  out[count++] = {0,  dy};
            return;
        }

        // Horizontal from parent
        if (dx != 0 && dy == 0) {
            if (canStep(cur.x, cur.y, dx, 0)) out[count++] = {dx, 0}; // natural

            // Forced diagonals if side is blocked but side-forward is free
            if (m_opt.allowDiagonal) {
                if (!pass(cur.x, cur.y + 1) && canStep(cur.x, cur.y, dx, 1))  out[count++] = {dx, 1};
                if (!pass(cur.x, cur.y - 1) && canStep(cur.x, cur.y, dx,-1)) out[count++] = {dx,-1};
            }
            return;
        }

        // Vertical from parent
        if (dx == 0 && dy != 0) {
            if (canStep(cur.x, cur.y, 0, dy)) out[count++] = {0, dy}; // natural

            if (m_opt.allowDiagonal) {
                if (!pass(cur.x + 1, cur.y) && canStep(cur.x, cur.y, 1, dy))  out[count++] = { 1, dy};
                if (!pass(cur.x - 1, cur.y) && canStep(cur.x, cur.y,-1, dy)) out[count++] = {-1, dy};
            }
            return;
        }
    }
};

} // namespace jps
