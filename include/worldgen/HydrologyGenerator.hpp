#pragma once
// ============================================================================
// HydrologyGenerator.hpp — rivers & lakes from a height map (C++17, STL-only)
// For Colony-Game
//
// What it computes on a W×H grid:
//  • filled_height01 : depression-filled heights (Priority-Flood) — guarantees drainage
//  • flow_dir_d8     : per-cell D8 direction code (see codebook below)
//  • flow_accum      : upstream contributing cell count (uint32_t)
//  • lake_mask       : 1 where depressions became lakes after filling
//  • river_mask      : 1 where channels exceed an accumulation threshold (no lakes)
//  • river_paths     : polylines following flow from sources/confluences to outlets
//  • strahler_order  : optional per-river-cell Strahler order (1=first-order stream)
//  • river_width     : estimated width (cells) from hydraulic-geometry scaling
//
// D8 direction code (dx,dy):
//    0:(+1, 0) E, 1:(+1,+1) SE, 2:(0,+1) S, 3:(-1,+1) SW,
//    4:(-1, 0) W, 5:(-1,-1) NW, 6:(0,-1) N, 7:(+1,-1) NE
//
// References (concepts; implementation here is original):
//  • Priority-Flood depression filling ensures a drain path for every cell.
//  • D8 flow directions and flow accumulation are standard for DEM hydrology.
//  • Strahler stream order sizes streams by confluence hierarchy.
//  • Hydraulic geometry uses power laws to relate width to discharge/area.
//    (See README comments for citations you can include in your docs.)
// ============================================================================

#include <vector>
#include <queue>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

namespace worldgen {

struct RiverPath {
    std::vector<std::pair<int,int>> points; // grid coords along centerline
    std::vector<float> width_cells;         // same length as points
    int strahler_order = 1;
};

struct HydroParams {
    int   width = 0, height = 0;
    float sea_level = 0.50f;                 // normalized: height <= sea_level => water
    float lake_eps = 1e-5f;                  // lake if (filled - original) > lake_eps
    bool  use_external_water = false;        // if true, treat external mask as water (see API)

    // River extraction
    uint32_t river_accum_threshold = 300;    // cells; tweak by map size / rainfall
    uint32_t min_path_len = 12;              // discard very short dangling segments

    // Width estimate (hydraulic geometry: W = k * A^b with area≈accum)
    float width_k = 0.35f;                   // base width scaling (cells)
    float width_b = 0.5f;                    // exponent; 0.4–0.6 common

    // Strahler ordering
    bool compute_strahler = true;
};

struct HydroResult {
    int width=0, height=0;
    std::vector<float>   filled_height01;    // W*H
    std::vector<uint8_t> flow_dir_d8;        // W*H
    std::vector<uint32_t> flow_accum;        // W*H
    std::vector<uint8_t> lake_mask;          // W*H
    std::vector<uint8_t> river_mask;         // W*H
    std::vector<uint8_t> strahler_order;     // W*H (0 if disabled)
    std::vector<RiverPath> river_paths;      // centerlines with width estimate
};

// ---------------------------- internals -----------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }

struct PQNode {
    float h; int x; int y;
    bool operator<(const PQNode& o) const { return h > o.h; } // min-heap
};

// Priority-Flood (Barnes et al. 2014): fill internal depressions to spill height
inline void priority_flood_fill(const std::vector<float>& h, int W,int H,
                                std::vector<float>& filled)
{
    filled.assign((size_t)W*H, 0.0f);
    std::vector<uint8_t> closed((size_t)W*H, 0);
    std::priority_queue<PQNode> pq;

    // seed PQ with boundary cells (drain to edges)
    for(int x=0;x<W;++x){
        int y0=0, y1=H-1;
        size_t i0=I(x,y0,W), i1=I(x,y1,W);
        pq.push({h[i0], x, y0}); closed[i0]=1; filled[i0]=h[i0];
        if (y1!=y0){ pq.push({h[i1], x, y1}); closed[i1]=1; filled[i1]=h[i1]; }
    }
    for(int y=1;y<H-1;++y){
        int x0=0, x1=W-1;
        size_t i0=I(x0,y,W), i1=I(x1,y,W);
        if (!closed[i0]){ pq.push({h[i0], x0, y}); closed[i0]=1; filled[i0]=h[i0]; }
        if (!closed[i1]){ pq.push({h[i1], x1, y}); closed[i1]=1; filled[i1]=h[i1]; }
    }

    const int dx8[8]={ 1,1,0,-1,-1,-1, 0, 1};
    const int dy8[8]={ 0,1,1, 1, 0,-1,-1,-1};

    while(!pq.empty()){
        PQNode n=pq.top(); pq.pop();
        for(int k=0;k<8;++k){
            int nx=n.x+dx8[k], ny=n.y+dy8[k];
            if(!inb(nx,ny,W,H)) continue;
            size_t j=I(nx,ny,W);
            if (closed[j]) continue;
            float nh = h[j];
            float fh = std::max(n.h, nh); // fill depression up to spill height
            filled[j]=fh; closed[j]=1;
            pq.push({fh, nx, ny});
        }
    }
}

// choose D8 downslope neighbor (ties broken deterministically)
inline uint8_t d8_dir(const std::vector<float>& Hf, int W,int H, int x,int y){
    static const int dx8[8]={ 1,1,0,-1,-1,-1, 0, 1};
    static const int dy8[8]={ 0,1,1, 1, 0,-1,-1,-1};
    float here = Hf[I(x,y,W)];
    float best = here; // must be strictly lower, but fill ensures outlet; tie handled
    int   bestk = 0;
    for(int k=0;k<8;++k){
        int nx=x+dx8[k], ny=y+dy8[k]; if(!inb(nx,ny,W,H)) continue;
        float h = Hf[I(nx,ny,W)];
        // prefer the steepest descent; add tiny directional bias to break flats
        float score = h + 1e-6f * (float)k;
        if (score < best){ best=score; bestk=k; }
    }
    // If everything is >= here (rare due to bias), still return bestk (flat tie)
    return (uint8_t)bestk;
}

inline void compute_d8_and_accum(const std::vector<float>& Hf, int W,int H,
                                 std::vector<uint8_t>& dir,
                                 std::vector<uint32_t>& accum)
{
    const size_t N=(size_t)W*H;
    dir.assign(N, 0u);
    accum.assign(N, 1u); // each cell contributes itself

    static const int dx8[8]={ 1,1,0,-1,-1,-1, 0, 1};
    static const int dy8[8]={ 0,1,1, 1, 0,-1,-1,-1};

    // downstream index for each cell
    std::vector<int> to(N, -1);
    std::vector<int> indeg(N, 0);

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        uint8_t k = d8_dir(Hf, W,H, x,y);
        dir[i] = k;
        int nx=x+dx8[k], ny=y+dy8[k];
        if (inb(nx,ny,W,H)){
            int j=(int)I(nx,ny,W);
            to[i]=j; indeg[(size_t)j]++;
        }
    }

    // Kahn-style topological pass for accumulation
    std::vector<int> q; q.reserve(N);
    for(size_t i=0;i<N;++i) if (indeg[i]==0) q.push_back((int)i);

    for(size_t qi=0; qi<q.size(); ++qi){
        int i=q[qi];
        int j=to[(size_t)i];
        if (j>=0){
            accum[(size_t)j] += accum[(size_t)i];
            if (--indeg[(size_t)j]==0) q.push_back(j);
        }
    }
}

// Trace polylines along river_mask
inline std::vector<RiverPath> extract_paths(const std::vector<uint8_t>& river_mask,
                                            const std::vector<uint8_t>& dir,
                                            const std::vector<uint32_t>& accum,
                                            int W,int H,
                                            uint32_t min_len,
                                            float wk,float wb)
{
    const size_t N=(size_t)W*H;
    static const int dx8[8]={ 1,1,0,-1,-1,-1, 0, 1};
    static const int dy8[8]={ 0,1,1, 1, 0,-1,-1,-1};

    // indegree restricted to river cells
    std::vector<int> indegR(N,0), to(N,-1);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        if (!river_mask[i]) continue;
        uint8_t k = dir[i];
        int nx=x+dx8[k], ny=y+dy8[k]; if(!inb(nx,ny,W,H)) continue;
        size_t j=I(nx,ny,W);
        if (river_mask[j]){ to[i]=(int)j; indegR[j]++; }
    }

    // start at sources (indegR==0) and confluences (indegR!=1)
    std::vector<uint8_t> used(N,0);
    std::vector<RiverPath> paths;

    auto widthFromA=[&](uint32_t A)->float{
        return wk * std::pow(std::max(1u,A), wb);
    };

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i0=I(x,y,W);
        if (!river_mask[i0] || used[i0]) continue;
        if (!(indegR[i0]==0 || indegR[i0]>1)) continue; // not a start

        RiverPath rp;
        int ix=x, iy=y; size_t i=i0; int steps=0;
        while(true){
            if (!river_mask[i] || used[i]) break;
            used[i]=1;
            rp.points.push_back({ix,iy});
            rp.width_cells.push_back(widthFromA(accum[i]));
            int j = to[i];
            if (j<0) break;
            ix = j%W; iy = j/W; i=(size_t)j;
            if (++steps > W*H) break; // safety
            if (indegR[i]>1){ // stop at next confluence; downstream path will start there
                rp.points.push_back({ix,iy});
                rp.width_cells.push_back(widthFromA(accum[i]));
                break;
            }
        }
        if (rp.points.size() >= min_len) paths.push_back(std::move(rp));
    }

    return paths;
}

inline void compute_strahler(const std::vector<uint8_t>& river_mask,
                             const std::vector<uint8_t>& dir,
                             int W,int H,
                             std::vector<uint8_t>& order_out)
{
    const size_t N=(size_t)W*H;
    static const int dx8[8]={ 1,1,0,-1,-1,-1, 0, 1};
    static const int dy8[8]={ 0,1,1, 1, 0,-1,-1,-1};

    order_out.assign(N,0);
    std::vector<int> to(N,-1), indeg(N,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        if (!river_mask[i]) continue;
        uint8_t k=dir[i];
        int nx=x+dx8[k], ny=y+dy8[k]; if(!inb(nx,ny,W,H)) continue;
        size_t j=I(nx,ny,W);
        if (river_mask[j]){ to[i]=(int)j; indeg[j]++; }
    }
    std::vector<int> q; q.reserve(N);
    for(size_t i=0;i<N;++i) if (river_mask[i] && indeg[i]==0){ q.push_back((int)i); order_out[i]=1; }
    // track max child order & how many times it appears
    std::vector<uint8_t> maxOrd(N,0), countMax(N,0);

    for(size_t qi=0; qi<q.size(); ++qi){
        int i=q[qi]; int j=to[(size_t)i];
        if (j<0){ continue; }
        uint8_t oi = order_out[(size_t)i];
        if (oi > maxOrd[(size_t)j]){ maxOrd[(size_t)j]=oi; countMax[(size_t)j]=1; }
        else if (oi == maxOrd[(size_t)j]){ countMax[(size_t)j]++; }

        if (--indeg[(size_t)j]==0){
            order_out[(size_t)j] = (countMax[(size_t)j] >= 2) ? (uint8_t)(maxOrd[(size_t)j]+1)
                                                             : maxOrd[(size_t)j];
            q.push_back(j);
        }
    }
}

} // namespace detail

// ------------------------------- API --------------------------------

inline HydroResult GenerateHydrology(
    const std::vector<float>& height01, int W, int H,
    const HydroParams& P = {},
    const std::vector<uint8_t>* externalWaterMask /*optional W*H, 1=water*/)
{
    HydroResult out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return out;

    // 1) Depression fill (Priority-Flood) → filled height
    detail::priority_flood_fill(height01, W,H, out.filled_height01);

    // Optionally treat provided water as already "water"
    std::vector<uint8_t> water(N,0);
    if (P.use_external_water && externalWaterMask) water = *externalWaterMask;
    else {
        for(size_t i=0;i<N;++i) water[i] = (height01[i] <= P.sea_level) ? 1u : 0u;
    }

    // 2) Flow directions (D8) & accumulation on filled DEM
    detail::compute_d8_and_accum(out.filled_height01, W,H, out.flow_dir_d8, out.flow_accum);

    // 3) Lakes = cells raised by the fill (not ocean) — simple depression water
    out.lake_mask.assign(N, 0u);
    for(size_t i=0;i<N;++i){
        if (!water[i] && (out.filled_height01[i] - height01[i]) > P.lake_eps) out.lake_mask[i]=1u;
    }

    // 4) Rivers = high-accum cells, excluding lakes
    out.river_mask.assign(N, 0u);
    for(size_t i=0;i<N;++i){
        if (out.flow_accum[i] >= P.river_accum_threshold && !out.lake_mask[i]) out.river_mask[i]=1u;
    }

    // 5) Optional Strahler order on the river subgraph
    out.strahler_order.assign(N, 0u);
    if (P.compute_strahler) detail::compute_strahler(out.river_mask, out.flow_dir_d8, W,H, out.strahler_order);

    // 6) Extract polyline centerlines with width estimate (width ~ k*A^b)
    out.river_paths = detail::extract_paths(out.river_mask, out.flow_dir_d8, out.flow_accum,
                                            W,H, P.min_path_len, P.width_k, P.width_b);

    return out;
}

/*
------------------------------------ Usage ------------------------------------

#include "worldgen/HydrologyGenerator.hpp"

void build_hydrology(const std::vector<float>& height01, int W, int H,
                     const std::vector<uint8_t>* waterMask /*optional*/)
{
    worldgen::HydroParams P;
    P.width=W; P.height=H;
    P.sea_level = 0.50f;
    P.river_accum_threshold = std::max<uint32_t>(200, (W*H)/9000); // scale with map
    P.width_k = 0.35f; P.width_b = 0.5f;
    P.compute_strahler = true;

    worldgen::HydroResult H = worldgen::GenerateHydrology(height01, W, H, P, waterMask);

    // Hooks:
    //  • Render H.lake_mask as water; H.river_paths as splines with width_cells for meshing.
    //  • Use H.flow_accum to place watermills/fisheries/floodplains.
    //  • Feed H.flow_dir_d8 & H.flow_accum to your road builder (bridges/ fords).
    //  • Use H.strahler_order to size bridges and set shipping/fishing rules.
}
*/
} // namespace worldgen
