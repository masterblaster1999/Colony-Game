#include "JPS.h"
#include <queue>
#include <unordered_map>
#include <optional>
#include <cmath>

namespace colony::nav {

static inline bool InBBox(const JPSOptions& opt, const Coord& c) {
    if (!opt.hasBBox) return true;
    return c.x >= opt.bboxMin.x && c.y >= opt.bboxMin.y && c.x <= opt.bboxMax.x && c.y <= opt.bboxMax.y;
}

static inline bool Passable(const IGridMap& m, const Coord& c) {
    return InBounds(m, c.x, c.y) && m.IsPassable(c.x, c.y);
}

static inline bool CanDiag(const IGridMap& m, const Coord& a, const Coord& step) {
    // No corner cutting: both cardinal steps must be passable.
    return m.IsPassable(a.x + step.x, a.y) && m.IsPassable(a.x, a.y + step.y);
}

struct Rec {
    Coord c;
    float g = 0, f = 0;
    Coord parent;       // parent node
    bool hasParent = false;
};

struct PQCmp {
    bool operator()(const Rec& a, const Rec& b) const noexcept { return a.f > b.f; }
};

// Returns a "jump point" reached from curr by stepping (dx,dy). If none, std::nullopt.
static std::optional<Coord> Jump(const IGridMap& m, const Coord& curr, const Coord& goal,
                                 int dx, int dy, const JPSOptions& opt) {
    Coord n{ curr.x + dx, curr.y + dy };
    if (!Passable(m, n) || !InBBox(opt, n)) return std::nullopt;
    if (n == goal) return n;

    // Check forced neighbors (Harabor & Grastien 2011):
    // Straight moves
    if (dx != 0 && dy == 0) {
        // horizontal
        // Forced if obstacles block straight adjacency but diagonal around is open.
        if ((!m.IsPassable(n.x, n.y + 1) && m.IsPassable(n.x - dx, n.y + 1)) ||
            (!m.IsPassable(n.x, n.y - 1) && m.IsPassable(n.x - dx, n.y - 1))) {
            return n;
        }
    } else if (dy != 0 && dx == 0) {
        // vertical
        if ((!m.IsPassable(n.x + 1, n.y) && m.IsPassable(n.x + 1, n.y - dy)) ||
            (!m.IsPassable(n.x - 1, n.y) && m.IsPassable(n.x - 1, n.y - dy))) {
            return n;
        }
    } else {
        // diagonal
        if (opt.diagonals == DiagonalPolicy::Never) return std::nullopt;
        if (!CanDiag(m, curr, {dx, dy})) return std::nullopt;
        // Diagonal forced neighbors
        if ((!m.IsPassable(n.x - dx, n.y) && m.IsPassable(n.x - dx, n.y + dy)) ||
            (!m.IsPassable(n.x, n.y - dy) && m.IsPassable(n.x + dx, n.y - dy))) {
            return n;
        }
        // Also, if we can jump horizontally/vertically from here, return this node.
        if (Jump(m, n, goal, dx, 0, opt)) return n;
        if (Jump(m, n, goal, 0, dy, opt)) return n;
    }
    // Continue jumping in the same direction.
    return Jump(m, n, goal, dx, dy, opt);
}

// Neighbor pruning per JPS. Returns at most a few directions relative to parent.
static void PrunedDirections(const IGridMap& m, const Coord& c, const std::optional<Coord>& parent,
                             std::vector<std::pair<int,int>>& out, const JPSOptions& opt) {
    out.clear();
    const int dirs8[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} };

    if (!parent.has_value()) {
        // start node: all natural neighbors (pruned later by CanDiag)
        for (int i=0;i<8;++i) out.emplace_back(dirs8[i][0], dirs8[i][1]);
        return;
    }
    int dx = (c.x - parent->x); dx = (dx>0)-(dx<0);
    int dy = (c.y - parent->y); dy = (dy>0)-(dy<0);

    if (dx != 0 && dy != 0) {
        // Diagonal move: natural neighbors are (dx,dy), (dx,0), (0,dy)
        out.emplace_back(dx, dy);
        out.emplace_back(dx, 0);
        out.emplace_back(0, dy);
        // Prune depending on obstacles (corner check happens later).
    } else if (dx != 0) {
        // Horizontal: (dx,0), and forced diagonals if needed handled by Jump
        out.emplace_back(dx, 0);
        // Also include diagonals to catch forced jumps
        out.emplace_back(dx, 1);
        out.emplace_back(dx, -1);
    } else if (dy != 0) {
        out.emplace_back(0, dy);
        out.emplace_back(1, dy);
        out.emplace_back(-1, dy);
    }
}

static inline float StepCost(int dx, int dy) {
    return (dx != 0 && dy != 0) ? kCostDiagonal : kCostStraight;
}

std::optional<Path> FindPathJPS(const IGridMap& m, const Coord& start, const Coord& goal, const JPSOptions& opt) {
    if (!InBounds(m, start.x, start.y) || !InBounds(m, goal.x, goal.y)) return std::nullopt;
    if (!m.IsPassable(start.x, start.y) || !m.IsPassable(goal.x, goal.y)) return std::nullopt;
    if (opt.hasBBox) {
        if (!InBBox(opt, start) || !InBBox(opt, goal)) return std::nullopt;
    }

    std::priority_queue<Rec, std::vector<Rec>, PQCmp> open;
    std::unordered_map<Coord, float, CoordHash> g;
    std::unordered_map<Coord, Coord, CoordHash> parent;
    g.reserve(1024); parent.reserve(1024);

    g[start] = 0.0f;
    open.push({ start, 0.0f, Octile(start, goal), {}, false });

    auto relax = [&](const Coord& from, const Coord& jp, float add) {
        float tentative = g[from] + add + m.ExtraCost(jp.x, jp.y);
        auto it = g.find(jp);
        if (it == g.end() || tentative < it->second) {
            g[jp] = tentative;
            parent[jp] = from;
            open.push({ jp, tentative, tentative + Octile(jp, goal) , {}, true });
        }
    };

    std::vector<std::pair<int,int>> dirs;
    while (!open.empty()) {
        Rec cur = open.top(); open.pop();
        if (cur.c == goal) {
            Path p;
            // reconstruct jump parents path then "string pull" (simple collinearity removal)
            std::vector<Coord> rev;
            Coord at = goal;
            while (at != start) {
                rev.push_back(at);
                at = parent.at(at);
            }
            rev.push_back(start);
            std::reverse(rev.begin(), rev.end());
            // simple collinearity prune
            for (size_t i=0;i<rev.size();++i) {
                if (p.points.size() < 2) { p.points.push_back(rev[i]); continue; }
                auto& a = p.points[p.points.size()-2];
                auto& b = p.points[p.points.size()-1];
                auto& c = rev[i];
                int abx = (b.x - a.x), aby = (b.y - a.y);
                int bcx = (c.x - b.x), bcy = (c.y - b.y);
                int n1x = (abx>0)-(abx<0), n1y = (aby>0)-(aby<0);
                int n2x = (bcx>0)-(bcx<0), n2y = (bcy>0)-(bcy<0);
                if (n1x == n2x && n1y == n2y) {
                    p.points.back() = c; // extend segment
                } else {
                    p.points.push_back(c);
                }
            }
            return p;
        }

        std::optional<Coord> parentNode;
        auto itp = parent.find(cur.c);
        if (itp != parent.end()) { parentNode = itp->second; }

        PrunedDirections(m, cur.c, parentNode, dirs, opt);
        for (auto [dx,dy] : dirs) {
            if (dx == 0 && dy == 0) continue;
            // Corner-cutting rule for diagonals at expansion time:
            if (dx != 0 && dy != 0) {
                if (opt.diagonals == DiagonalPolicy::Never) continue;
                if (!CanDiag(m, cur.c, {dx,dy})) continue;
            }
            if (auto jp = Jump(m, cur.c, goal, dx, dy, opt)) {
                // accumulate cost along ray (straight/diag uniform cost, so we can compute geometrically)
                int steps = std::max(std::abs(jp->x - cur.c.x), std::abs(jp->y - cur.c.y));
                float add = StepCost(dx,dy) * steps;
                relax(cur.c, *jp, add);
            }
        }
    }
    return std::nullopt;
}

} // namespace colony::nav
