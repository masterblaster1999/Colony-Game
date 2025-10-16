// pathfinding/procgen/RoadCarver.h
#pragma once
#include <vector>
#include <queue>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include "PoissonDisk.h"

namespace colony::pathfinding::procgen {

struct GridView {
    int w, h;
    const std::vector<uint8_t>* obstacle;     // 1 = blocked, 0 = free
    const std::vector<uint16_t>* cost;        // >=1
};

struct GridEdit {
    int w, h;
    std::vector<uint8_t>* obstacle;           // edit in-place
    std::vector<uint16_t>* cost;
};

inline size_t I(int x, int y, int w) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); }
inline bool inBounds(int x, int y, int w, int h) { return (x >= 0 && y >= 0 && x < w && y < h); }

inline float hDist(int x1, int y1, int x2, int y2) { // Manhattan heuristic (4-neighborhood)
    return static_cast<float>(std::abs(x1 - x2) + std::abs(y1 - y2));
}

struct Node {
    int x, y; float g, f;
    int px, py;
};
struct NodeCmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };

// Minimal A* that prefers lower movement cost; treats obstacles as very expensive but carve-able.
inline std::vector<Int2> astarPath(const GridView& grid, Int2 start, Int2 goal, float obstaclePenalty = 1000.0f)
{
    const int w = grid.w, h = grid.h;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> gScore(static_cast<size_t>(w) * h, INF);
    std::vector<Int2> parent(static_cast<size_t>(w) * h, Int2{ -1, -1 });
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    auto push = [&](int x, int y, float g, float f, int px, int py) {
        open.push(Node{ x, y, g, f, px, py });
    };

    gScore[I(start.x, start.y, w)] = 0.0f;
    push(start.x, start.y, 0.0f, hDist(start.x, start.y, goal.x, goal.y), -1, -1);

    const int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    while (!open.empty()) {
        Node n = open.top(); open.pop();
        if (n.x == goal.x && n.y == goal.y) break;
        if (n.g > gScore[I(n.x, n.y, w)]) continue;

        for (auto& d : dirs) {
            int nx = n.x + d[0], ny = n.y + d[1];
            if (!inBounds(nx, ny, w, h)) continue;

            float step = static_cast<float>((*grid.cost)[I(nx, ny, w)]);
            if ((*grid.obstacle)[I(nx, ny, w)]) step += obstaclePenalty; // still traversable, just very expensive
            float ng = n.g + std::max(1.0f, step);

            size_t ni = I(nx, ny, w);
            if (ng < gScore[ni]) {
                gScore[ni] = ng;
                parent[ni] = Int2{ n.x, n.y };
                float f = ng + hDist(nx, ny, goal.x, goal.y);
                push(nx, ny, ng, f, n.x, n.y);
            }
        }
    }

    // Reconstruct
    std::vector<Int2> path;
    Int2 cur = goal;
    size_t ci = I(cur.x, cur.y, w);
    if (std::isinf(gScore[ci])) return path; // no path
    while (!(cur.x == -1 && cur.y == -1)) {
        path.push_back(cur);
        Int2 p = parent[I(cur.x, cur.y, w)];
        cur = p;
        if (cur.x == -1 && cur.y == -1) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Build an MST over POIs with Prim's; then carve with A* and lower costs along the result.
inline void carveRoads(GridEdit edit, const std::vector<Int2>& pois, uint16_t roadCost = 1, int radius = 1)
{
    if (pois.size() < 2) return;
    const int n = static_cast<int>(pois.size());
    std::vector<int> parent(n, -1);
    std::vector<float> key(n, std::numeric_limits<float>::infinity());
    std::vector<char> inMST(n, 0);

    auto dist = [&](int i, int j) {
        float dx = float(pois[i].x - pois[j].x), dy = float(pois[i].y - pois[j].y);
        return std::sqrt(dx * dx + dy * dy);
    };

    key[0] = 0.0f;
    for (int c = 0; c < n - 1; ++c) {
        int u = -1; float best = std::numeric_limits<float>::infinity();
        for (int v = 0; v < n; ++v) if (!inMST[v] && key[v] < best) { best = key[v]; u = v; }
        if (u == -1) break;
        inMST[u] = 1;
        for (int v = 0; v < n; ++v) if (!inMST[v]) {
            float d = dist(u, v);
            if (d < key[v]) { key[v] = d; parent[v] = u; }
        }
    }

    GridView view{ edit.w, edit.h, edit.obstacle, edit.cost };

    for (int v = 1; v < n; ++v) {
        int u = parent[v];
        if (u < 0) continue;
        auto path = astarPath(view, pois[u], pois[v], /*obstaclePenalty*/ 500.0f);

        for (auto p : path) {
            for (int ry = -radius; ry <= radius; ++ry)
            for (int rx = -radius; rx <= radius; ++rx) {
                int xx = p.x + rx, yy = p.y + ry;
                if (!inBounds(xx, yy, edit.w, edit.h)) continue;
                (*edit.obstacle)[I(xx, yy, edit.w)] = 0; // clear
                (*edit.cost)    [I(xx, yy, edit.w)] = std::min<uint16_t>((*edit.cost)[I(xx, yy, edit.w)], roadCost);
            }
        }
    }
}

} // namespace colony::pathfinding::procgen
