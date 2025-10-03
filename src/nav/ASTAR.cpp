#include "ASTAR.h"
#include <queue>
#include <unordered_map>
#include <cmath>

namespace colony::nav {

struct NodeRec {
    Coord c;
    float g = 0, f = 0;
};

struct FSort {
    bool operator()(const NodeRec& a, const NodeRec& b) const noexcept { return a.f > b.f; }
};

static inline bool CanStep(const IGridMap& m, const Coord& a, const Coord& b, DiagonalPolicy diag) {
    if (!InBounds(m, b.x, b.y) || !m.IsPassable(b.x, b.y)) return false;
    int dx = b.x - a.x, dy = b.y - a.y;
    if ((dx == 0) || (dy == 0)) return true;
    if (diag == DiagonalPolicy::Never) return false;
    // Avoid corner cutting: both side-adjacent tiles must be passable.
    return m.IsPassable(a.x + dx, a.y) && m.IsPassable(a.x, a.y + dy);
}

static inline void Reconstruct(const Coord& start, const Coord& goal,
                               const std::unordered_map<Coord, Coord, CoordHash>& parent,
                               Path& out) {
    out.points.clear();
    Coord cur = goal;
    while (cur != start) {
        out.points.push_back(cur);
        cur = parent.at(cur);
    }
    out.points.push_back(start);
    std::reverse(out.points.begin(), out.points.end());
}

std::optional<Path> FindPathAStar(const IGridMap& m, const Coord& start, const Coord& goal, const AStarOptions& opt) {
    if (!InBounds(m, start.x, start.y) || !InBounds(m, goal.x, goal.y)) return std::nullopt;
    if (!m.IsPassable(start.x, start.y) || !m.IsPassable(goal.x, goal.y)) return std::nullopt;

    std::priority_queue<NodeRec, std::vector<NodeRec>, FSort> open;
    std::unordered_map<Coord, float, CoordHash> gScore;
    std::unordered_map<Coord, Coord, CoordHash> parent;
    gScore.reserve(1024); parent.reserve(1024);

    gScore[start] = 0.0f;
    open.push({ start, 0.0f, Octile(start, goal) });

    auto push = [&](const Coord& from, const Coord& to, float cost) {
        float tentative = gScore[from] + cost + m.ExtraCost(to.x, to.y);
        auto it = gScore.find(to);
        if (it == gScore.end() || tentative < it->second) {
            gScore[to] = tentative;
            parent[to] = from;
            float f = tentative + Octile(to, goal);
            open.push({ to, tentative, f });
        }
    };

    const int dirs8[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} };

    while (!open.empty()) {
        NodeRec cur = open.top(); open.pop();
        if (cur.c == goal) {
            Path p; Reconstruct(start, goal, parent, p);
            return p;
        }
        // Expand neighbors
        for (int i = 0; i < 8; ++i) {
            Coord nxt{ cur.c.x + dirs8[i][0], cur.c.y + dirs8[i][1] };
            if (!CanStep(m, cur.c, nxt, opt.diagonals)) continue;
            float step = (dirs8[i][0] != 0 && dirs8[i][1] != 0) ? kCostDiagonal : kCostStraight;
            push(cur.c, nxt, step);
        }
    }
    return std::nullopt;
}

} // namespace colony::nav
