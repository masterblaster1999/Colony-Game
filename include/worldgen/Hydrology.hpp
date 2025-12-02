#pragma once
// ============================================================================
// Hydrology.hpp — header-only rivers & lakes generator for 2D heightmaps
// Drop into include/worldgen/ and #include "worldgen/Hydrology.hpp"
//
// Pipeline:
//   1) Priority-Flood fill (removes pits/“digital dams”)  [Barnes et al.]
//   2) Flow directions (D8, tie-broken deterministically) [Jenson & Domingue]
//   3) Flow accumulation (Kahn-style topological propagation)
//   4) Lake detection (filled - original) + small-lake filter
//   5) River mask by accumulation threshold + optional channel carving
//
// Returns: carved terrain, water depth, masks, flow directions & accumulation.
//
// References (concepts):
//  • Barnes, Lehman, Mulla (2014): Priority-Flood depression filling
//  • Jenson & Domingue (1988): D8 flow direction and accumulation
//  • Tarboton (1997): D∞ background (kept D8 here for speed/simple integration)
// ============================================================================

#include <vector>
#include <queue>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cmath>

namespace worldgen {

struct HydroParams {
    // --- Lakes ---
    float lake_min_depth = 0.25f;     // cells with (filled - original) >= this are wet candidates
    int   lake_min_area  = 64;        // remove lakes smaller than this many cells

    // --- Rivers ---
    std::uint32_t river_min_accum = 600; // min contributing cells to call a river (tune to map size)
    float min_down_slope = 0.0f;         // require this local normalized downslope (0 disables)
    // Channel shaping
    float channel_depth   = 2.0f;    // thalweg depth at max accumulation
    float depth_exponent  = 0.6f;    // 0..1, how depth scales with normalized accumulation
    int   bank_radius     = 2;       // widen channel banks out this many rings
    float bank_falloff    = 0.6f;    // 0..1, fraction per ring
    float min_height_clamp = -10000.0f;

    // --- Numerics ---
    float flat_tie_epsilon = 1e-5f;  // tiny for D8 tie-breaking
};

struct HydroResult {
    std::vector<float>    carved_height;   // terrain with river carving
    std::vector<float>    water_depth;     // lakes (fill) + river depth
    std::vector<std::uint32_t> accumulation; // upstream contributing cell count (>=1)
    std::vector<std::uint8_t>  flow_dir_d8;   // 0..7=N,NE,E,SE,S,SW,W,NW; 255 = sink/edge
    std::vector<std::uint8_t>  river_mask;    // 0/1
    std::vector<std::uint8_t>  lake_mask;     // 0/1
    int width = 0, height = 0;
    std::uint32_t max_accum = 0;
};

namespace detail {

inline std::size_t idx(int x, int y, int W) { return (std::size_t)y * (std::size_t)W + (std::size_t)x; }

static const int dx8[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
static const int dy8[8] = {-1,-1, 0, 1, 1, 1, 0,-1 };

inline bool in_bounds(int x, int y, int W, int H) {
    return x>=0 && y>=0 && x<W && y<H;
}

// simple 32‑bit mix; deterministic per index
inline std::uint32_t hash32(std::uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
    return v;
}

// ------------------------- Priority‑Flood fill -------------------------
// Barnes et al.: flood inwards from the boundary using a min‑heap.
// Filled DEM has no depressions; every cell can drain outward.  :contentReference[oaicite:1]{index=1}
inline std::vector<float> priority_flood_fill(const std::vector<float>& h, int W, int H) {
    const std::size_t N = (std::size_t)W * (std::size_t)H;
    std::vector<float> filled = h;
    std::vector<std::uint8_t> closed(N, 0);

    struct Node { float z; int x, y; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const { return a.z > b.z; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    // Seed border
    for (int x=0; x<W; ++x) {
        pq.push({filled[idx(x,0,W)], x,0});           closed[idx(x,0,W)] = 1;
        pq.push({filled[idx(x,H-1,W)], x,H-1});       closed[idx(x,H-1,W)] = 1;
    }
    for (int y=1; y<H-1; ++y) {
        pq.push({filled[idx(0,y,W)], 0,y});           closed[idx(0,y,W)] = 1;
        pq.push({filled[idx(W-1,y,W)], W-1,y});       closed[idx(W-1,y,W)] = 1;
    }

    while (!pq.empty()) {
        Node n = pq.top(); pq.pop();
        for (int k=0; k<8; ++k) {
            const int nx = n.x + dx8[k], ny = n.y + dy8[k];
            if (!in_bounds(nx,ny,W,H)) continue;
            const std::size_t ni = idx(nx,ny,W);
            if (closed[ni]) continue;
            closed[ni] = 1;

            // Raise cell to at least current water level to remove pits
            if (filled[ni] < n.z) filled[ni] = n.z;
            pq.push({filled[ni], nx, ny});
        }
    }
    return filled;
}

// ----------------------------- D8 directions -----------------------------
// Choose steepest downslope neighbor on the filled DEM.
// Tie-break deterministically with a tiny hashed epsilon to resolve flats. :contentReference[oaicite:2]{index=2}
inline std::vector<std::uint8_t> compute_d8(const std::vector<float>& z, int W, int H, float eps) {
    const std::size_t N = (std::size_t)W*(std::size_t)H;
    std::vector<std::uint8_t> dir(N, 255);

    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const std::size_t i = idx(x,y,W);
        const float zi = z[i];
        float best = std::numeric_limits<float>::infinity();
        int bestk = -1;

        for (int k=0; k<8; ++k) {
            const int nx = x + dx8[k], ny = y + dy8[k];
            if (!in_bounds(nx,ny,W,H)) continue;
            const std::size_t ni = idx(nx,ny,W);

            // neighbor height with tiny deterministic jitter (hash) to break ties
            const float zhat = z[ni] + eps * ( (hash32((std::uint32_t)ni) & 0xFFFFu) / 65535.0f - 0.5f );
            if (zhat < best) { best = zhat; bestk = k; }
        }

        // Only flow if we can go "downhill" after tie-breaking; otherwise leave as 255
        if (bestk >= 0 && best < zi) dir[i] = (std::uint8_t)bestk;
    }
    return dir;
}

// ---------------------------- Accumulation ----------------------------
// Kahn-like topological pass: every cell contributes 1 to itself and pushes
// to its single D8 downstream neighbor.
inline std::pair<std::vector<std::uint32_t>, std::uint32_t>
flow_accumulation(const std::vector<std::uint8_t>& dir, int W, int H) {
    const std::size_t N = (std::size_t)W*(std::size_t)H;
    std::vector<std::uint32_t> indeg(N, 0);
    std::vector<int> downstream(N, -1);

    auto to_index = [&](int x,int y)->int { return in_bounds(x,y,W,H) ? (int)idx(x,y,W) : -1; };

    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const int i = (int)idx(x,y,W);
        const std::uint8_t d = dir[i];
        if (d == 255) { downstream[i] = -1; continue; }
        const int j = to_index(x + dx8[d], y + dy8[d]);
        downstream[i] = j;
        if (j >= 0) indeg[(std::size_t)j]++;
    }

    std::queue<int> q;
    for (int i=0; i<(int)N; ++i) if (indeg[(std::size_t)i] == 0) q.push(i);

    std::vector<std::uint32_t> acc(N, 1u);
    std::uint32_t max_acc = 1u;

    while (!q.empty()) {
        const int i = q.front(); q.pop();
        const int j = downstream[i];
        if (j >= 0) {
            acc[(std::size_t)j] += acc[(std::size_t)i];
            max_acc = std::max(max_acc, acc[(std::size_t)j]);
            auto& d = indeg[(std::size_t)j];
            if (d>0) { d--; if (d==0) q.push(j); }
        }
    }
    return { std::move(acc), max_acc };
}

inline float local_downslope_norm(const std::vector<float>& z, int W, int H, int x, int y, std::uint8_t d8) {
    if (d8 == 255) return 0.0f;
    const int nx = x + dx8[d8], ny = y + dy8[d8];
    if (!in_bounds(nx,ny,W,H)) return 0.0f;
    const float dz = z[idx(x,y,W)] - z[idx(nx,ny,W)];
    const float step = (d8%2==0) ? 1.0f : std::sqrt(2.0f);
    return std::max(0.0f, dz) / step; // normalized by grid step
}

// Remove lake components smaller than min_area (8‑connected BFS)
inline void filter_small_lakes(std::vector<std::uint8_t>& mask, int W, int H, int min_area) {
    std::vector<std::uint8_t> keep(mask.size(), 0), seen(mask.size(), 0);
    std::queue<std::pair<int,int>> q;

    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const std::size_t i = idx(x,y,W);
        if (!mask[i] || seen[i]) continue;
        int area = 0;
        std::vector<std::size_t> comp;
        q.push({x,y}); seen[i] = 1;

        while (!q.empty()) {
            auto [cx,cy] = q.front(); q.pop();
            const std::size_t ci = idx(cx,cy,W);
            comp.push_back(ci); area++;

            for (int k=0; k<8; ++k) {
                const int nx = cx + dx8[k], ny = cy + dy8[k];
                if (!in_bounds(nx,ny,W,H)) continue;
                const std::size_t ni = idx(nx,ny,W);
                if (mask[ni] && !seen[ni]) { seen[ni] = 1; q.push({nx,ny}); }
            }
        }
        if (area >= min_area) for (auto ci : comp) keep[ci] = 1;
    }
    mask.swap(keep);
}

// Carve channels and soft banks along river cells using depth~accum^exp
inline void carve_channels(const std::vector<std::uint32_t>& acc,
                           std::uint32_t max_acc,
                           const std::vector<std::uint8_t>& river_mask,
                           int W, int H,
                           const HydroParams& P,
                           std::vector<float>& height_io,
                           std::vector<float>& waterdepth_io) {
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const std::size_t i = idx(x,y,W);
        if (!river_mask[i]) continue;

        const float norm = (max_acc>0) ? (acc[i] / (float)max_acc) : 0.0f;
        const float d_center = P.channel_depth * std::pow(norm, P.depth_exponent);

        height_io[i]     = std::max(P.min_height_clamp, height_io[i] - d_center);
        waterdepth_io[i] = std::max(waterdepth_io[i], d_center);

        // gentle banks
        for (int r=1; r<=P.bank_radius; ++r) {
            const float d_bank = d_center * std::pow(P.bank_falloff, (float)r);
            if (d_bank <= 0.0f) break;

            for (int oy=-r; oy<=r; ++oy) for (int ox=-r; ox<=r; ++ox) {
                if (std::max(std::abs(ox), std::abs(oy)) != r) continue; // ring only
                const int nx = x+ox, ny = y+oy;
                if (!in_bounds(nx,ny,W,H)) continue;
                const std::size_t ni = idx(nx,ny,W);
                height_io[ni]     = std::max(P.min_height_clamp, height_io[ni] - d_bank);
                waterdepth_io[ni] = std::max(waterdepth_io[ni], d_bank * 0.3f);
            }
        }
    }
}

} // namespace detail

// ----------------------------- Entry point -----------------------------
inline HydroResult GenerateHydrology(const std::vector<float>& height, int W, int H, const HydroParams& P = {}) {
    HydroResult out; out.width = W; out.height = H;
    const std::size_t N = (std::size_t)W * (std::size_t)H;

    if (W<=2 || H<=2 || height.size()!=N) {
        out.carved_height = height;
        out.water_depth.assign(N, 0.0f);
        out.accumulation.assign(N, 1u);
        out.flow_dir_d8.assign(N, 255);
        out.river_mask.assign(N, 0);
        out.lake_mask.assign(N, 0);
        out.max_accum = 1;
        return out;
    }

    // 1) Fill depressions
    std::vector<float> filled = detail::priority_flood_fill(height, W, H);

    // 2) Flow directions (D8 with tie-breaking)
    out.flow_dir_d8 = detail::compute_d8(filled, W, H, P.flat_tie_epsilon);

    // 3) Accumulation
    auto [acc, max_acc] = detail::flow_accumulation(out.flow_dir_d8, W, H);
    out.accumulation = std::move(acc);
    out.max_accum    = max_acc;

    // 4) Lakes from fill depth
    out.lake_mask.assign(N, 0);
    std::vector<float> waterdepth(N, 0.0f);
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const std::size_t i = detail::idx(x,y,W);
        const float d = std::max(0.0f, filled[i] - height[i]);
        if (d >= P.lake_min_depth) { out.lake_mask[i] = 1; waterdepth[i] = d; }
    }
    if (P.lake_min_area > 1) detail::filter_small_lakes(out.lake_mask, W, H, P.lake_min_area);
    for (std::size_t i=0; i<N; ++i) if (!out.lake_mask[i]) waterdepth[i] = 0.0f;

    // 5) Rivers by accumulation (and optional slope gate)
    out.river_mask.assign(N, 0);
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const std::size_t i = detail::idx(x,y,W);
        if (out.accumulation[i] < P.river_min_accum) continue;
        if (P.min_down_slope > 0.0f) {
            float s = detail::local_downslope_norm(filled, W, H, x, y, out.flow_dir_d8[i]);
            if (s < P.min_down_slope) continue;
        }
        out.river_mask[i] = 1;
    }

    // 6) Carve channels into the original terrain
    out.carved_height = height;
    detail::carve_channels(out.accumulation, out.max_accum, out.river_mask, W, H, P, out.carved_height, waterdepth);

    // Final clamp
    for (std::size_t i=0; i<N; ++i)
        if (out.carved_height[i] < P.min_height_clamp) out.carved_height[i] = P.min_height_clamp;

    out.water_depth = std::move(waterdepth);
    return out;
}

/*
------------------------------- Usage ---------------------------------

#include "worldgen/Hydrology.hpp"
using namespace worldgen;

void build_world(std::vector<float>& height, int W, int H, unsigned world_seed) {
    HydroParams p;
    // good starting points; tune to taste / map size
    p.river_min_accum = std::max<std::uint32_t>(600, (std::uint32_t)std::round(0.001f * W * H));
    p.channel_depth   = 2.0f;  // try 1.5–3.5
    p.bank_radius     = 2;     // try 1–3
    p.lake_min_area   = 64;

    HydroResult hydro = GenerateHydrology(height, W, H, p);

    // 1) Replace terrain (carved)
    height = hydro.carved_height;

    // 2) Use hydro.river_mask / hydro.lake_mask to paint water tiles or spawn water meshes
    // 3) Use hydro.water_depth for visuals/gameplay (bridges/boats/fishing/fertility)
    // 4) hydro.accumulation is great for siting towns/fields near water
}

Notes:
• Works with any float height grid (row-major, index = y*W + x).
• Keep D8 for speed; if you later need smoother drainage, consider D∞ (Tarboton 1997).  // ref only
*/
} // namespace worldgen
