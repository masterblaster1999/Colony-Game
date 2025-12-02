#pragma once
/*
    RoadNetworkGenerator.hpp — header-only, dependency-free procedural roads
    For: Colony-Game (C++17+)

    What it does
    ------------
    • Connects N terminals (town center, mines, forests, ruins, ports) with a compact,
      cost-aware network using repeated shortest paths on a grid (A* / Dijkstra).
    • Costs include slope/grade, water & soft terrain penalties, and a "reuse" discount
      so new routes tend to snap to existing roads (natural trunk–branch shapes).
    • Automatically tags bridge segments when crossing water, with a max span limit.
    • Optional: carves a shallow roadbed into the terrain for readability and movement.
    • Optional: returns smoothed/simplified polylines for decals or mesh roads.

    Background (concepts; this implementation is original)
    ------------------------------------------------------
    • Shortest paths via A* (Hart, Nilsson, Raphael, 1968).        // admissible h=0 here
    • Repeated shortest paths are a common Steiner-tree heuristic.  // Kou, Markowsky, Berman
    • Chaikin corner-cutting smoothing; Douglas–Peucker simplification.

    Usage example at the bottom of the file.
    References: see README snippet in your PR message.
*/

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>
#include <tuple>

namespace procgen {

// ----------------------------- Parameters & Types -----------------------------

struct Terminal {
    int x = 0, y = 0;       // grid coords
    uint8_t kind = 0;       // optional categorization (town=0, mine=1, etc.)
};

struct RoadParams {
    // Movement model
    bool   eight_neighbors = true;        // D8 vs. D4
    float  diag_cost       = 1.41421356f; // step cost for diagonals

    // Terrain/slope costs
    float  slope_weight    = 24.0f;       // multiplies local |grad|; tune to map scale
    float  max_grade       = 0.35f;       // extra penalty for slopes beyond this

    // Obstacles/water
    float  water_penalty   = 1000.0f;     // make water very unattractive
    float  soft_penalty    = 4.0f;        // e.g., forest, rough ground
    float  bridge_penalty  = 120.0f;      // added per water cell when crossing
    int    bridge_max_len  = 18;          // forbid spans longer than this many cells

    // Network shaping
    float  reuse_discount  = 0.45f;       // discount cost near existing roads
    float  near_road_bonus_radius = 2.0f; // radius for reuse discount propagation

    // Carving/visuals (optional)
    bool   carve_roadbed   = true;
    float  bed_half_width  = 1.0f;        // ~1→3x3 footprint
    float  bed_depth       = 0.006f;      // subtract from normalized height (0..1)
    float  bed_falloff     = 0.55f;       // per-ring falloff
    int    smooth_iters    = 2;           // Chaikin smoothing iterations
    float  simplify_eps    = 0.75f;       // Douglas–Peucker epsilon in cells

    // Safety
    float  clamp_min_height = -10000.f;
};

struct RoadResult {
    int width = 0, height = 0;

    std::vector<uint8_t> road_mask;     // 0/1 per cell
    std::vector<uint8_t> bridge_mask;   // 0/1 per cell (subset of road on water)
    std::vector<float>   carved_height; // height after optional roadbed carve

    // Optional: smoothed/simplified polylines for rendering
    std::vector<std::vector<std::pair<float,float>>> polylines;

    // Stats
    int roads_cells = 0;
    int bridges_cells = 0;
};

// ----------------------------- Utilities -----------------------------

static inline int idx(int x, int y, int W) { return y * W + x; }
static inline bool in_bounds(int x, int y, int W, int H) { return x>=0 && y>=0 && x<W && y<H; }
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

// Octile distance (if you later want non-zero heuristic); not used by default.
static inline float octile(int x0, int y0, int x1, int y1, float D, float D2) {
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    return (float)(D * (dx + dy) + (D2 - 2.0f * D) * std::min(dx, dy));
}

// Chaikin smoothing for open polylines (non-cyclic)
static inline std::vector<std::pair<float,float>>
chaikin(const std::vector<std::pair<float,float>>& P, int iters) {
    if (P.size() < 2 || iters <= 0) return P;
    std::vector<std::pair<float,float>> cur = P;
    for (int it = 0; it < iters; ++it) {
        std::vector<std::pair<float,float>> out;
        out.reserve(cur.size()*2);
        out.push_back(cur.front());
        for (size_t i = 0; i + 1 < cur.size(); ++i) {
            auto a = cur[i], b = cur[i+1];
            out.emplace_back(0.75f*a.first + 0.25f*b.first,
                              0.75f*a.second + 0.25f*b.second);
            out.emplace_back(0.25f*a.first + 0.75f*b.first,
                              0.25f*a.second + 0.75f*b.second);
        }
        out.push_back(cur.back());
        cur.swap(out);
    }
    return cur;
}

// Douglas–Peucker simplification
static inline float segdist2(const std::pair<float,float>& p,
                             const std::pair<float,float>& a,
                             const std::pair<float,float>& b) {
    float vx = b.first - a.first, vy = b.second - a.second;
    float wx = p.first - a.first, wy = p.second - a.second;
    float c1 = vx*wx + vy*wy;
    if (c1 <= 0) { float dx = p.first - a.first, dy = p.second - a.second; return dx*dx + dy*dy; }
    float c2 = vx*vx + vy*vy;
    if (c2 <= 0) { float dx = p.first - a.first, dy = p.second - a.second; return dx*dx + dy*dy; }
    float t = c1 / c2;
    float px = a.first + t*vx, py = a.second + t*vy;
    float dx = p.first - px, dy = p.second - py;
    return dx*dx + dy*dy;
}
static inline void dp_rec(const std::vector<std::pair<float,float>>& pts, int s, int e, float eps2, std::vector<char>& keep) {
    if (e <= s + 1) return;
    float maxd = 0; int best = -1;
    for (int i = s + 1; i < e; ++i) {
        float d = segdist2(pts[i], pts[s], pts[e]);
        if (d > maxd) { maxd = d; best = i; }
    }
    if (maxd > eps2) {
        keep[best] = 1;
        dp_rec(pts, s, best, eps2, keep);
        dp_rec(pts, best, e, eps2, keep);
    }
}
static inline std::vector<std::pair<float,float>>
douglas_peucker(const std::vector<std::pair<float,float>>& pts, float eps) {
    if (pts.size() <= 2) return pts;
    std::vector<char> keep(pts.size(), 0);
    keep.front() = keep.back() = 1;
    dp_rec(pts, 0, (int)pts.size()-1, eps*eps, keep);
    std::vector<std::pair<float,float>> out; out.reserve(pts.size());
    for (size_t i=0;i<pts.size();++i) if (keep[i]) out.push_back(pts[i]);
    return out;
}

// ----------------------------- A* (with h=0 → Dijkstra) -----------------------------

struct Node { int x,y; float f,g; int parent; };
struct NodeCmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };

// Find a shortest path from (sx,sy) to ANY cell with goalMask[i]==1.
// Returns true and fills out_path (list of cell indices) on success.
static inline bool astar_to_mask(
    int W, int H,
    int sx, int sy,
    const std::vector<float>& step_cost,   // per-cell additive cost
    const std::vector<uint8_t>& solidMask, // 1 = blocked
    const std::vector<uint8_t>& goalMask,  // 1 = goal
    bool eight_neighbors,
    float diag_cost,
    int bridge_max_run,
    const std::vector<uint8_t>* waterMask, // optional, caps consecutive water
    std::vector<int>& out_path)
{
    const int N = W*H;
    auto inb = [&](int x,int y){return in_bounds(x,y,W,H);};
    auto id  = [&](int x,int y){return idx(x,y,W);};

    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<float> f(N, std::numeric_limits<float>::infinity());
    std::vector<int> parent(N, -1);
    std::vector<uint8_t> closed(N, 0);

    auto hfun = [&](int /*x*/,int /*y*/)->float { return 0.0f; }; // admissible; A*→Dijkstra

    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;
    if (!inb(sx,sy) || solidMask[id(sx,sy)]) return false;

    int s = id(sx,sy);
    g[s] = 0.0f; f[s] = hfun(sx,sy);
    open.push({sx,sy,f[s],g[s],-1});

    const int DX8[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
    const int DY8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
    const int DX4[4] = { 1, 0,-1, 0};
    const int DY4[4] = { 0, 1, 0,-1};

    while (!open.empty()) {
        Node n = open.top(); open.pop();
        int i = id(n.x,n.y);
        if (closed[i]) continue;
        closed[i] = 1; parent[i] = n.parent;

        if (goalMask[i]) {
            out_path.clear();
            int cur = i;
            while (cur != -1) { out_path.push_back(cur); cur = parent[cur]; }
            std::reverse(out_path.begin(), out_path.end());
            return true;
        }

        const int K = eight_neighbors ? 8 : 4;
        for (int k=0;k<K;++k) {
            int nx = n.x + (eight_neighbors?DX8[k]:DX4[k]);
            int ny = n.y + (eight_neighbors?DY8[k]:DY4[k]);
            if (!inb(nx,ny)) continue;
            int j = id(nx,ny);
            if (solidMask[j]) continue;

            // Limit overly long water runs (approximate, backward-looking)
            if (waterMask && (*waterMask)[j]) {
                int run = 1;
                int px = n.x, py = n.y;
                while (inb(px,py) && waterMask && (*waterMask)[idx(px,py,W)]) {
                    ++run;
                    px -= (eight_neighbors?DX8[k]:DX4[k]);
                    py -= (eight_neighbors?DY8[k]:DY4[k]);
                    if (run > bridge_max_run) break;
                }
                if (run > bridge_max_run) continue;
            }

            float move = (eight_neighbors && (k%2==1 || k==3 || k==5 || k==7)) ? diag_cost : 1.0f;
            float tentative = g[i] + move + step_cost[j];
            if (tentative < g[j]) {
                g[j] = tentative;
                float fscore = tentative + hfun(nx,ny);
                f[j] = fscore;
                open.push({nx,ny,fscore,tentative,i});
            }
        }
    }
    return false;
}

// ----------------------------- Main API -----------------------------

// heightN: required, normalized [0..1]. Size = W*H.
// waterMask: optional; 1 for water (discouraged/bridged), 0 for land.
// softMask:  optional; 1 for “soft” terrain (forest, rough ground).
// solidMask: optional; 1 blocks roads entirely (cliffs, buildings).
inline RoadResult GenerateRoadNetwork(
    const std::vector<float>& heightN,
    int W, int H,
    const std::vector<Terminal>& terminals,
    const RoadParams& P = {},
    const std::vector<uint8_t>* waterMask = nullptr,
    const std::vector<uint8_t>* softMask  = nullptr,
    const std::vector<uint8_t>* solidMask = nullptr)
{
    const int N = W*H;
    RoadResult out;
    out.width = W; out.height = H;

    if (W<=1 || H<=1 || (int)heightN.size()!=N || terminals.empty()) {
        out.carved_height = heightN;
        out.road_mask.assign(N,0);
        out.bridge_mask.assign(N,0);
        return out;
    }

    // 1) Per-cell base cost from slope and masks
    std::vector<float> base_cost(N, 0.0f);
    auto Hs = [&](int x,int y)->float {
        x = std::clamp(x,0,W-1); y = std::clamp(y,0,H-1);
        return heightN[idx(x,y,W)];
    };
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        int i = idx(x,y,W);
        float gx = 0.5f * (Hs(x+1,y) - Hs(x-1,y));
        float gy = 0.5f * (Hs(x,y+1) - Hs(x,y-1));
        float slope = std::sqrt(gx*gx + gy*gy);

        float cost = P.slope_weight * slope;
        if (slope > P.max_grade) cost += (slope - P.max_grade) * P.slope_weight * 8.0f;
        if (waterMask && (*waterMask)[i]) cost += P.water_penalty;
        if (softMask  && (*softMask)[i])  cost += P.soft_penalty;
        base_cost[i] = cost;
    }

    std::vector<uint8_t> solids(N, 0);
    if (solidMask) solids = *solidMask;

    // 2) Connect terminals into a road tree (repeated shortest paths to the built network)
    out.road_mask.assign(N,0);
    out.bridge_mask.assign(N,0);
    out.carved_height = heightN;

    // Initialize with the first terminal as the root
    std::vector<Terminal> T = terminals;
    int rootx = std::clamp(T[0].x, 0, W-1);
    int rooty = std::clamp(T[0].y, 0, H-1);
    out.road_mask[idx(rootx,rooty,W)] = 1;

    // Goal set = current road cells
    std::vector<uint8_t> goalMask(N,0);
    auto refresh_goals = [&](){
        std::fill(goalMask.begin(), goalMask.end(), 0);
        for (int i=0;i<N;++i) if (out.road_mask[i]) goalMask[i] = 1;
    };
    refresh_goals();

    // Mutable step_cost adds a reuse discount near existing roads
    std::vector<float> step_cost = base_cost;
    auto apply_reuse_discount = [&](){
        if (P.reuse_discount <= 0) return;
        const int R = (int)std::round(P.near_road_bonus_radius);
        for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
            int i = idx(x,y,W);
            if (!out.road_mask[i]) continue;
            for (int oy=-R; oy<=R; ++oy) for (int ox=-R; ox<=R; ++ox) {
                int nx=x+ox, ny=y+oy; if (!in_bounds(nx,ny,W,H)) continue;
                int j = idx(nx,ny,W);
                step_cost[j] = std::max(0.0f, base_cost[j] * (1.0f - P.reuse_discount));
            }
        }
    };
    apply_reuse_discount();

    // Connect each remaining terminal
    for (size_t ti=1; ti<T.size(); ++ti) {
        int sx = std::clamp(T[ti].x, 0, W-1);
        int sy = std::clamp(T[ti].y, 0, H-1);

        std::vector<int> path;
        bool ok = astar_to_mask(W,H, sx,sy, step_cost, solids, goalMask,
                                P.eight_neighbors, P.diag_cost,
                                P.bridge_max_len, waterMask, path);
        if (!ok) continue; // unreachable → skip

        // Mark path & tag bridges
        for (int cell : path) {
            out.road_mask[cell] = 1;
            if (waterMask && (*waterMask)[cell]) out.bridge_mask[cell] = 1;
        }

        // Save a polyline for rendering (smoothed & simplified)
        std::vector<std::pair<float,float>> pl; pl.reserve(path.size());
        for (int cell : path) { int x = cell%W, y = cell/W; pl.emplace_back((float)x,(float)y); }
        if (P.smooth_iters > 0) pl = chaikin(pl, P.smooth_iters);
        if (P.simplify_eps > 0) pl = douglas_peucker(pl, P.simplify_eps);
        out.polylines.push_back(std::move(pl));

        // Update costs & goals to encourage reuse in future connections
        apply_reuse_discount();
        refresh_goals();
    }

    out.roads_cells   = (int)std::count(out.road_mask.begin(), out.road_mask.end(), (uint8_t)1);
    out.bridges_cells = (int)std::count(out.bridge_mask.begin(), out.bridge_mask.end(), (uint8_t)1);

    // 3) Optional: carve a shallow roadbed into the terrain (visual/gameplay)
    if (P.carve_roadbed) {
        const int R = std::max(1, (int)std::round(P.bed_half_width));
        for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
            int i = idx(x,y,W);
            if (!out.road_mask[i]) continue;

            float base = out.carved_height[i] - P.bed_depth;
            out.carved_height[i] = std::max(P.clamp_min_height, base);

            for (int r=1; r<=R; ++r) {
                float d = P.bed_depth * std::pow(P.bed_falloff, (float)r);
                for (int oy=-r; oy<=r; ++oy) for (int ox=-r; ox<=r; ++ox) {
                    if (std::max(std::abs(ox), std::abs(oy)) != r) continue; // ring only
                    int nx=x+ox, ny=y+oy; if (!in_bounds(nx,ny,W,H)) continue;
                    int j = idx(nx,ny,W);
                    out.carved_height[j] = std::max(P.clamp_min_height, out.carved_height[j] - d);
                }
            }
        }
        // keep water unchanged if provided
        if (waterMask) for (int i=0;i<N;++i) if ((*waterMask)[i]) out.carved_height[i] = heightN[i];
    }

    return out;
}

// ----------------------------- Example -----------------------------
/*
#include "procgen/RoadNetworkGenerator.hpp"

void build_roads(MyWorld& world) {
    int W = world.width(), H = world.height();
    const std::vector<float>& heightN = world.heightNormalized();  // [0..1]
    const std::vector<uint8_t>& water = world.waterMask();         // 0=land,1=water

    std::vector<procgen::Terminal> T = {
        { world.colonyX(), world.colonyY(), 0 },  // root/colony
        { world.ironMineX(), world.ironMineY(), 1 },
        { world.forestX(),   world.forestY(),   2 },
        { world.ruinsX(),    world.ruinsY(),    3 }
    };

    procgen::RoadParams P;
    P.bridge_max_len = 12;  // prefer fords/switchbacks over long bridges
    P.bed_depth      = 0.008f;

    auto roads = procgen::GenerateRoadNetwork(heightN, W, H, T, P, &water);

    world.applyHeight(roads.carved_height);
    world.paintRoads(roads.road_mask, roads.bridge_mask, roads.polylines);
    world.setMovementDiscount(roads.road_mask, /*mult=*/0.4f); // gameplay hook
}
*/
} // namespace procgen
