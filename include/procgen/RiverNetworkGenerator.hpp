// ============================================================================
// RiverNetworkGenerator.hpp
// Header-only, dependency-free river & lake procedural generation for 2D grids
// License: MIT (or match your project license)
// ============================================================================

#pragma once
#include <vector>
#include <queue>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cmath>
#include <utility>

namespace procgen {

struct RiverNetworkParams {
    // Numerics
    float  flat_epsilon       = 1e-4f; // tiny tie-breaker to route across flats
    float  min_down_slope     = 0.0f;  // require this normalized downslope for river cell (0 disables)

    // Lakes
    float  lake_min_depth     = 0.25f; // (filled - original) >= this → wet candidate
    int    lake_min_area      = 64;    // discard lakes smaller than this many cells

    // Rivers
    uint32_t river_min_accum  = 600;   // min flow accumulation to mark river (tune to map size)
    float  channel_depth      = 2.0f;  // base thalweg depth at max accumulation
    float  depth_exponent     = 0.60f; // how depth scales with normalized accumulation (0..1)
    int    bank_radius        = 2;     // how many neighbor rings to widen into
    float  bank_falloff       = 0.6f;  // 0..1, fraction of depth applied per ring

    // Safety
    float  min_height_clamp   = -10000.f;
};

struct RiverNetworkResult {
    // Size = W*H
    std::vector<float>    carved_height;   // original minus channel carving
    std::vector<float>    water_depth;     // lakes (filled-original) + river depth
    std::vector<uint32_t> accumulation;    // upstream cell counts (>=1)
    std::vector<uint8_t>  flow_dir_d8;     // 0..7 encode N,NE,E,SE,S,SW,W,NW ; 255 = sink/edge fallback
    std::vector<uint8_t>  river_mask;      // 0/1
    std::vector<uint8_t>  lake_mask;       // 0/1

    int width = 0, height = 0;
    uint32_t max_accum = 0;
};

namespace detail {

inline size_t idx(int x, int y, int W) { return (size_t)y * (size_t)W + (size_t)x; }

static const int dx8[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
static const int dy8[8] = {-1,-1, 0, 1, 1, 1, 0,-1 };

inline bool in_bounds(int x, int y, int W, int H) {
    return x>=0 && y>=0 && x<W && y<H;
}

// Tiny deterministic jitter [-1,1] for tie-breaking on flats.
inline float tiny_hash01(uint32_t v) {
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    return (v & 0xFFFFu) / 65535.0f; // [0,1]
}
inline float tiny_hash_signed(uint32_t v) { return 2.0f * tiny_hash01(v) - 1.0f; }

// Priority-Flood depression filling (Barnes et al.): O(n log n) for floats.
// Produces "filled" elevations with no pits; every cell can drain outward.
inline std::vector<float> priority_flood_fill(const std::vector<float>& h, int W, int H) {
    const size_t N = (size_t)W * (size_t)H;
    std::vector<float> filled = h;
    std::vector<uint8_t> closed(N, 0);

    struct Node { float z; int x,y; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const { return a.z > b.z; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    // Seed all border cells
    for (int x=0; x<W; ++x) {
        pq.push({filled[idx(x,0,W)], x,0});         closed[idx(x,0,W)] = 1;
        pq.push({filled[idx(x,H-1,W)], x,H-1});     closed[idx(x,H-1,W)] = 1;
    }
    for (int y=1; y<H-1; ++y) {
        pq.push({filled[idx(0,y,W)], 0,y});         closed[idx(0,y,W)] = 1;
        pq.push({filled[idx(W-1,y,W)], W-1,y});     closed[idx(W-1,y,W)] = 1;
    }

    while (!pq.empty()) {
        Node n = pq.top(); pq.pop();
        for (int k=0;k<8;++k) {
            const int nx = n.x + dx8[k], ny = n.y + dy8[k];
            if (!in_bounds(nx,ny,W,H)) continue;
            const size_t ni = idx(nx,ny,W);
            if (closed[ni]) continue;
            closed[ni] = 1;

            // Raise neighbor to at least current water level to eliminate pits.
            const float z = filled[ni];
            const float newz = std::max(z, n.z);
            filled[ni] = newz;

            pq.push({filled[ni], nx, ny});
        }
    }
    return filled;
}

// D8 flow directions on filled DEM; add tiny jitter to break flats deterministically.
// 255 denotes "no outflow" fallback (edge/sink).
inline std::vector<uint8_t> compute_d8(const std::vector<float>& z, int W, int H, float flat_epsilon) {
    const size_t N = (size_t)W*(size_t)H;
    std::vector<uint8_t> dir(N, 255);

    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const size_t i = idx(x,y,W);
        const float zi = z[i];
        float best = std::numeric_limits<float>::infinity();
        int bestk = -1;

        for (int k=0;k<8;++k) {
            const int nx = x + dx8[k], ny = y + dy8[k];
            if (!in_bounds(nx,ny,W,H)) continue;
            const size_t ni = idx(nx,ny,W);

            // tiny deterministic jitter to create a gradient across flats
            const float eps = flat_epsilon * tiny_hash_signed((uint32_t)ni * 9781u + 0x9E3779B9u);
            const float zk  = z[ni] + eps;

            if (zk < best) { best = zk; bestk = k; }
        }

        // only accept if strictly downhill after jitter; else leave as 255 (local sink/edge)
        if (bestk >= 0 && best < zi) dir[i] = (uint8_t)bestk;
    }
    return dir;
}

// Kahn topological pass for accumulation (each cell contributes weight 1 to itself and downstream).
inline std::pair<std::vector<uint32_t>, uint32_t>
flow_accumulation(const std::vector<uint8_t>& dir, int W, int H) {
    const size_t N = (size_t)W*(size_t)H;
    std::vector<uint32_t> indeg(N,0);
    std::vector<int> downstream(N,-1);

    auto to_index = [&](int x,int y)->int {
        return (x<0||y<0||x>=W||y>=H) ? -1 : (int)idx(x,y,W);
    };

    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const int i = (int)idx(x,y,W);
        const uint8_t d = dir[i];
        if (d==255) { downstream[i] = -1; continue; }
        const int nx = x + dx8[d], ny = y + dy8[d];
        const int j  = to_index(nx,ny);
        downstream[i] = j;
        if (j>=0) indeg[(size_t)j]++;
    }

    std::queue<int> q;
    for (int i=0;i<(int)N;++i) if (indeg[(size_t)i]==0) q.push(i);

    std::vector<uint32_t> acc(N,1u);
    uint32_t max_acc = 1u;

    while (!q.empty()) {
        const int i = q.front(); q.pop();
        const int j = downstream[i];
        if (j>=0) {
            acc[(size_t)j] += acc[(size_t)i];
            max_acc = std::max(max_acc, acc[(size_t)j]);
            auto& d = indeg[(size_t)j];
            if (d>0) { d--; if (d==0) q.push(j); }
        }
    }
    return { std::move(acc), max_acc };
}

inline float local_downslope_norm(const std::vector<float>& z, int W, int H, int x, int y, uint8_t dir) {
    if (dir==255) return 0.0f;
    const int nx = x + dx8[dir], ny = y + dy8[dir];
    if (!in_bounds(nx,ny,W,H)) return 0.0f;

    const float dz = z[idx(x,y,W)] - z[idx(nx,ny,W)];
    const float step = (dir%2==0) ? 1.0f : std::sqrt(2.0f);
    return std::max(0.0f, dz) / step;
}

// Keep only lake components ≥ min_area (8-connected BFS).
inline void filter_small_lakes(std::vector<uint8_t>& lake_mask, int W, int H, int min_area) {
    std::vector<uint8_t> keep(lake_mask.size(), 0), visited(lake_mask.size(), 0);
    std::queue<std::pair<int,int>> q;

    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const size_t i = idx(x,y,W);
        if (!lake_mask[i] || visited[i]) continue;

        int area = 0;
        std::vector<size_t> comp;
        q.push({x,y}); visited[i]=1;

        while (!q.empty()) {
            auto [cx,cy] = q.front(); q.pop();
            const size_t ci = idx(cx,cy,W);
            comp.push_back(ci); area++;

            for (int k=0;k<8;++k) {
                const int nx = cx + dx8[k], ny = cy + dy8[k];
                if (!in_bounds(nx,ny,W,H)) continue;
                const size_t ni = idx(nx,ny,W);
                if (lake_mask[ni] && !visited[ni]) { visited[ni]=1; q.push({nx,ny}); }
            }
        }
        if (area >= min_area) for (auto ci : comp) keep[ci]=1;
    }
    lake_mask.swap(keep);
}

// Carve channels along river cells with gentle banks.
inline void carve_channels(const std::vector<uint32_t>& acc,
                           uint32_t max_acc,
                           const std::vector<uint8_t>& river_mask,
                           int W, int H,
                           const RiverNetworkParams& P,
                           std::vector<float>& carved_height,
                           std::vector<float>& water_depth) {
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const size_t i = idx(x,y,W);
        if (!river_mask[i]) continue;

        const float norm = (max_acc>0) ? (acc[i] / (float)max_acc) : 0.0f;
        const float d_center = P.channel_depth * std::pow(norm, P.depth_exponent);

        carved_height[i] = std::max(P.min_height_clamp, carved_height[i] - d_center);
        water_depth[i]   = std::max(water_depth[i], d_center);

        for (int r=1; r<=P.bank_radius; ++r) {
            const float d_bank = d_center * std::pow(P.bank_falloff, (float)r);
            if (d_bank <= 0.0f) break;

            for (int oy=-r; oy<=r; ++oy) for (int ox=-r; ox<=r; ++ox) {
                if (std::max(std::abs(ox), std::abs(oy)) != r) continue; // ring only
                const int nx = x+ox, ny = y+oy;
                if (!in_bounds(nx,ny,W,H)) continue;
                const size_t ni = idx(nx,ny,W);
                carved_height[ni] = std::max(P.min_height_clamp, carved_height[ni] - d_bank);
                water_depth[ni]   = std::max(water_depth[ni], d_bank * 0.3f);
            }
        }
    }
}

} // namespace detail

// -------------------------------
// High-level entry point
// -------------------------------
inline RiverNetworkResult GenerateRiversAndLakes(const std::vector<float>& height,
                                                 int W, int H,
                                                 const RiverNetworkParams& P = {}) {
    RiverNetworkResult out;
    out.width = W; out.height = H;

    const size_t N = (size_t)W*(size_t)H;
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

    // 2) Flow directions (D8)
    out.flow_dir_d8 = detail::compute_d8(filled, W, H, P.flat_epsilon);

    // 3) Flow accumulation
    auto [acc, max_acc] = detail::flow_accumulation(out.flow_dir_d8, W, H);
    out.accumulation = std::move(acc);
    out.max_accum    = max_acc;

    // 4) Lake candidates from (filled - original)
    std::vector<float> waterdepth(N, 0.0f);
    out.lake_mask.assign(N, 0);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const size_t i = detail::idx(x,y,W);
        const float d = std::max(0.0f, filled[i] - height[i]);
        if (d >= P.lake_min_depth) { out.lake_mask[i]=1; waterdepth[i]=d; }
    }
    if (P.lake_min_area > 1) detail::filter_small_lakes(out.lake_mask, W, H, P.lake_min_area);
    for (size_t i=0;i<N;++i) if (!out.lake_mask[i]) waterdepth[i]=0.0f;

    // 5) Rivers by accumulation (and optional slope gate)
    out.river_mask.assign(N, 0);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        const size_t i = detail::idx(x,y,W);
        if (out.accumulation[i] < P.river_min_accum) continue;
        if (P.min_down_slope > 0.0f) {
            const float s = detail::local_downslope_norm(filled, W, H, x, y, out.flow_dir_d8[i]);
            if (s < P.min_down_slope) continue;
        }
        out.river_mask[i] = 1;
    }

    // 6) Carve channels
    out.carved_height = height;
    detail::carve_channels(out.accumulation, out.max_accum, out.river_mask, W, H, P, out.carved_height, waterdepth);

    // Clamp
    for (size_t i=0;i<N;++i)
        if (out.carved_height[i] < P.min_height_clamp) out.carved_height[i] = P.min_height_clamp;

    out.water_depth = std::move(waterdepth);
    return out;
}

// -------------------------------
// Example usage
// -------------------------------
/*
#include "procgen/RiverNetworkGenerator.hpp"

void build_world(const std::vector<float>& srcHeight, int W, int H) {
    procgen::RiverNetworkParams p;
    p.river_min_accum = std::max<uint32_t>(600, (uint32_t)(0.002f * W * H));
    p.channel_depth   = 2.0f;  // try 1.5–3.5
    p.bank_radius     = 2;     // try 1–3

    auto hydro = procgen::GenerateRiversAndLakes(srcHeight, W, H, p);

    // 1) Use hydro.carved_height as terrain
    // 2) Use hydro.river_mask / hydro.lake_mask for water tiles/meshes
    // 3) Use hydro.water_depth for visuals/gameplay (bridges, fishing, shipping)
    // 4) hydro.accumulation is handy for placing vegetation or settlements near water
}
*/
} // namespace procgen
