#pragma once
#include "GridMap.hpp"
#include "Heuristic.hpp"
#include "Path.hpp"
#include <queue>
#include <functional>
#include <limits>

namespace colony::pf {

struct AStarConfig {
    bool allow_diagonals = true; // grid uses 8-dir + no-corner-cutting
};

class AStar {
public:
    explicit AStar(const GridMap& map, AStarConfig cfg = {}) : _m(map), _cfg(cfg) {}

    // Returns empty path if no solution.
    Path find_path(IVec2 start, IVec2 goal) {
        const int w = _m.width(), h = _m.height();
        if (!_m.passable(start.x,start.y) || !_m.passable(goal.x,goal.y)) return {};

        const NodeId sid = to_id(start.x, start.y, w);
        const NodeId gid = to_id(goal.x,  goal.y,  w);

        _cost.assign(static_cast<size_t>(w*h), StepCost{});
        std::vector<u8> state(static_cast<size_t>(w*h), 0); // 0=unseen,1=open,2=closed

        struct QN { float f; NodeId id; bool operator<(const QN& o) const { return f > o.f; } };
        std::priority_queue<QN> open;

        _cost[sid].g = 0.0f;
        _cost[sid].f = octile(sid, gid, w);
        _cost[sid].parent = kInvalid;
        open.push({ _cost[sid].f, sid });
        state[sid] = 1;

        constexpr int DIRS = 8;
        static const int dx[DIRS] = { 1,-1,0,0,  1, 1,-1,-1 };
        static const int dy[DIRS] = { 0,0,1,-1,  1,-1, 1,-1  };

        while (!open.empty()) {
            const auto [f, cur] = open.top(); open.pop();
            if (state[cur] == 2) continue; // skip stale
            state[cur] = 2;
            if (cur == gid) return reconstruct(gid, sid, w, _cost);

            const auto C = from_id(cur, w);

            for (int dir = 0; dir < DIRS; ++dir) {
                if (!_cfg.allow_diagonals && dir >= 4) break; // limit to 4-dir
                const int nx = C.x + dx[dir], ny = C.y + dy[dir];
                if (!_m.can_step(C.x, C.y, dx[dir], dy[dir])) continue;

                const NodeId nid = to_id(nx, ny, w);
                if (state[nid] == 2) continue;

                const float g_new = _cost[cur].g + _m.step_cost(C.x, C.y, dx[dir], dy[dir]);

                if (state[nid] != 1 || g_new < _cost[nid].g) {
                    _cost[nid].g = g_new;
                    _cost[nid].f = g_new + octile(nid, gid, w);
                    _cost[nid].parent = cur;
                    open.push({ _cost[nid].f, nid });
                    state[nid] = 1;
                }
            }
        }
        return {}; // no path
    }

private:
    const GridMap& _m;
    AStarConfig _cfg;
    std::vector<StepCost> _cost;
};

} // namespace colony::pf
