#pragma once
#include "GridMap.hpp"
#include "Heuristic.hpp"
#include "Path.hpp"
#include <queue>

// Minimal Jump Point Search (JPS) for 8-dir grids, following Harabor & Grastien.
// Canonical pruning + forced-neighbor checks; no tie-breaking tricks.
// For robust theory & extensions, see the original / follow-up papers. :contentReference[oaicite:10]{index=10}

namespace colony::pf {

struct JpsConfig { bool allow_diagonals = true; };

class JPS {
public:
    explicit JPS(const GridMap& map, JpsConfig cfg = {}) : _m(map), _cfg(cfg) {}

    Path find_path(IVec2 start, IVec2 goal) {
        const int w = _m.width(), h = _m.height();
        if (!_m.passable(start.x,start.y) || !_m.passable(goal.x,goal.y)) return {};
        const NodeId sid = to_id(start.x, start.y, w);
        const NodeId gid = to_id(goal.x,  goal.y,  w);

        _cost.assign(static_cast<size_t>(w*h), StepCost{});
        std::vector<u8> closed(static_cast<size_t>(w*h), 0);

        struct QN { float f; NodeId id; IVec2 dir; bool operator<(const QN& o) const { return f > o.f; } };
        std::priority_queue<QN> open;

        _cost[sid].g = 0.0f;
        _cost[sid].f = octile(sid, gid, w);
        _cost[sid].parent = kInvalid;
        open.push({ _cost[sid].f, sid, {0,0} });

        while (!open.empty()) {
            auto [f, cur, dir] = open.top(); open.pop();
            if (closed[cur]) continue;
            closed[cur] = 1;
            if (cur == gid) return reconstruct(gid, sid, w, _cost);

            const auto C = from_id(cur, w);

            // Identify pruned neighbors (set of directions to try).
            // NOTE: all_dirs() returns a fixed-size array while pruned_dirs() returns a vector;
            // keep the two branches separate to avoid a conditional-expression type mismatch.
            if (dir.x == 0 && dir.y == 0)
            {
                const auto dirs = all_dirs();
                for (auto d : dirs)
                {
                    NodeId jp{};
                    float step_g{};
                    if (!jump(C.x, C.y, d.x, d.y, gid, jp, step_g))
                        continue;
                    if (closed[jp])
                        continue;

                    const float g_new = _cost[cur].g + step_g;
                    if (_cost[jp].parent == kInvalid || g_new < _cost[jp].g)
                    {
                        _cost[jp].g = g_new;
                        _cost[jp].f = g_new + octile(jp, gid, w);
                        _cost[jp].parent = cur;
                        open.push({ _cost[jp].f, jp, d });
                    }
                }
            }
            else
            {
                const auto dirs = pruned_dirs(C, dir);
                for (auto d : dirs)
                {
                    NodeId jp{};
                    float step_g{};
                    if (!jump(C.x, C.y, d.x, d.y, gid, jp, step_g))
                        continue;
                    if (closed[jp])
                        continue;

                    const float g_new = _cost[cur].g + step_g;
                    if (_cost[jp].parent == kInvalid || g_new < _cost[jp].g)
                    {
                        _cost[jp].g = g_new;
                        _cost[jp].f = g_new + octile(jp, gid, w);
                        _cost[jp].parent = cur;
                        open.push({ _cost[jp].f, jp, d });
                    }
                }
            }
        }
        return {};
    }

private:
    const GridMap& _m;
    JpsConfig _cfg;
    std::vector<StepCost> _cost;

    // Direction helpers
    static std::array<IVec2,8> all_dirs() {
        return { IVec2{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
    }

    // Prune neighbors in the current direction (canonical pruning)
    std::vector<IVec2> pruned_dirs(IVec2 c, IVec2 dir) const {
        std::vector<IVec2> out;
        if (dir.x==0 && dir.y==0) return { }; // shouldn't happen
        const bool diag = (dir.x!=0 && dir.y!=0);
        if (diag) {
            out.push_back(dir);
            out.push_back({dir.x, 0});
            out.push_back({0, dir.y});
            // Forced neighbors: if obstacles block, we must add perpendiculars
            if (!_m.passable(c.x - dir.x, c.y) && _m.passable(c.x - dir.x, c.y + dir.y))
                out.push_back({-dir.x, dir.y});
            if (!_m.passable(c.x, c.y - dir.y) && _m.passable(c.x + dir.x, c.y - dir.y))
                out.push_back({dir.x, -dir.y});
        } else {
            out.push_back(dir);
            // Forced neighbors for straight movement
            if (dir.x != 0) {
                if (!_m.passable(c.x, c.y + 1) && _m.passable(c.x + dir.x, c.y + 1)) out.push_back({dir.x, 1});
                if (!_m.passable(c.x, c.y - 1) && _m.passable(c.x + dir.x, c.y - 1)) out.push_back({dir.x,-1});
            } else {
                if (!_m.passable(c.x + 1, c.y) && _m.passable(c.x + 1, c.y + dir.y)) out.push_back({ 1, dir.y});
                if (!_m.passable(c.x - 1, c.y) && _m.passable(c.x - 1, c.y + dir.y)) out.push_back({-1, dir.y});
            }
        }
        return out;
    }

    // Move in (dx,dy) until a jump point is found, unreachable, or goal seen.
    bool jump(int x, int y, int dx, int dy, NodeId goal, NodeId& out, float& step_g) const {
        const int w = _m.width();
        step_g = 0.0f;
        int cx = x, cy = y;
        for (;;) {
            cx += dx; cy += dy;
            if (!_m.can_step(cx - dx, cy - dy, dx, dy)) return false; // hit wall / corner
            step_g += _m.step_cost(cx - dx, cy - dy, dx, dy);

            const NodeId nid = to_id(cx, cy, w);
            if (nid == goal) { out = nid; return true; }

            // Forced neighbor? -> jump point
            if (has_forced(cx, cy, dx, dy)) { out = nid; return true; }

            // Diagonal: if either straight direction yields a jump point we also stop
            if (dx != 0 && dy != 0) {
                NodeId tmp; float gtmp;
                if (jump(cx, cy, dx, 0, goal, tmp, gtmp)) { out = nid; return true; }
                if (jump(cx, cy, 0, dy, goal, tmp, gtmp)) { out = nid; return true; }
            }
            // Otherwise continue stepping
        }
    }

    bool has_forced(int x, int y, int dx, int dy) const {
        // See Harabor & Grastien for detailed cases; here are standard ones.
        if (dx != 0 && dy != 0) {
            // diagonal: a neighbor blocked orthogonally creates forced neighbor
            return ( !_m.passable(x - dx, y) && _m.passable(x - dx, y + dy) )
                || ( !_m.passable(x, y - dy) && _m.passable(x + dx, y - dy) );
        } else if (dx != 0) {
            // horizontal
            return ( !_m.passable(x, y + 1) && _m.passable(x + dx, y + 1) )
                || ( !_m.passable(x, y - 1) && _m.passable(x + dx, y - 1) );
        } else {
            // vertical
            return ( !_m.passable(x + 1, y) && _m.passable(x + 1, y + dy) )
                || ( !_m.passable(x - 1, y) && _m.passable(x - 1, y + dy) );
        }
    }
};

} // namespace colony::pf
