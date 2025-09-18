#include "ai/Pathfinding.h"

#include <array>
#include <algorithm>
#include <queue>
#include <cstdlib>   // std::abs

namespace colony::ai {

static inline int manhattan(const Point& a, const Point& b) noexcept {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

struct Node {
    Point p;
    int g;   // cost from start
    int f;   // g + heuristic
    Point parent;
};

struct Compare {
    bool operator()(const Node& lhs, const Node& rhs) const noexcept {
        return lhs.f > rhs.f; // min-heap via greater-than
    }
};

Path aStar(const IGrid& grid, Point start, Point goal) {
    auto inBounds = [&](int x, int y) noexcept {
        return x >= 0 && y >= 0 && x < grid.width() && y < grid.height();
    };

    std::priority_queue<Node, std::vector<Node>, Compare> open;
    std::unordered_map<Point, int,   PointHash> gScore;
    std::unordered_map<Point, Point, PointHash> cameFrom;

    gScore[start] = 0;
    open.push(Node{start, 0, manhattan(start, goal), {-1, -1}});

    const std::array<Point, 4> dirs{Point{1,0}, Point{-1,0}, Point{0,1}, Point{0,-1}};

    while (!open.empty()) {
        Node cur = open.top();
        open.pop();

        if (cur.p == goal) {
            Path path;
            Point step = cur.p;
            while (!(step == start)) {
                path.push_back(step);
                step = cameFrom[step];
            }
            path.push_back(start);
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (const auto& d : dirs) {
            int nx = cur.p.x + d.x;
            int ny = cur.p.y + d.y;
            if (!inBounds(nx, ny) || !grid.isWalkable(nx, ny)) continue;

            Point np{nx, ny};
            int tentative = cur.g + 1;

            auto it = gScore.find(np);
            if (it == gScore.end() || tentative < it->second) {
                gScore[np] = tentative;
                cameFrom[np] = cur.p;
                open.push(Node{np, tentative, tentative + manhattan(np, goal), cur.p});
            }
        }
    }

    return {}; // No path found
}

} // namespace colony::ai
