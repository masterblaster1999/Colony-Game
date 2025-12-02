#pragma once
// ============================================================================
// TownLayoutGenerator.hpp — header-only organic streets → blocks → parcels
// For Colony-Game (C++17 / STL-only)
// ----------------------------------------------------------------------------
// Pipeline on a W×H grid:
//   1) Demand & cost fields: prefers gentle land & water access, penalizes
//      steep slopes/water.
//   2) Street network: connect high-demand targets and external "portals"
//      via weighted shortest paths.
//   3) Dilate paths into a road mask (width in cells).
//   4) Flood-fill remaining buildable land into BLOCKS.
//   5) Voronoi parcels per block + a few Lloyd (CVT) relaxation steps.
// ----------------------------------------------------------------------------

#include <vector>
#include <queue>
#include <random>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

namespace worldgen {

struct I2 { int x{}, y{}; };

struct TownParams {
    int      width = 0, height = 0;
    I2       center{0,0};
    float    city_radius = 160.0f;     // cells; limit of town layout
    uint64_t seed = 0xBEEFCAFEu;

    // Terrain interpretation
    float    sea_level = 0.50f;        // height01 < sea_level => water (if waterMask is null)
    float    meters_per_height_unit = 1200.0f; // for slope normalization

    // Demand (where people want roads/parcels)
    float    demand_sigma = 0.33f;     // relative to city_radius (Gaussian falloff)
    float    water_attract = 0.35f;    // prefer near water (0..1)
    float    slope_avoid   = 0.6f;     // penalize steep slopes in demand

    // Road growth
    int      terminals = 28;           // number of demand peaks to connect
    float    terminal_min_spacing = 18.0f;
    float    slope_cost  = 4.0f;       // cost weight for slope (0..1 scale)
    float    water_cost  = 1000.0f;    // crossing water is essentially forbidden
    float    diag_cost   = 1.41421356f;// diagonal move multiplier
    int      road_width  = 2;          // dilation radius in cells

    // Blocks
    int      block_min_area = 50;      // ignore tiny slivers

    // Parcels
    float    target_parcel_area = 90.0f;   // in cells (~ footprint)
    float    parcel_min_spacing = 4.5f;    // blue-noise spacing for seeds
    int      lloyd_iters = 2;              // 0..3 recommended
};

struct TownLayout {
    int width=0, height=0;

    // Size W*H
    std::vector<uint8_t> road_mask;   // 1 where road surface
    std::vector<int>     block_id;    // -1 non-buildable / road / water, else 0..B-1
    std::vector<int>     parcel_id;   // -1 not a parcel, else 0..P-1 (unique globally)

    // Paths as polylines in grid coords
    std::vector<std::vector<I2>> roads;

    // Optional summary
    int blocks=0, parcels=0;
};

// --------------------------- Internals ---------------------------

namespace detail {

inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
inline float sqr(float v){ return v*v; }

struct RNG { std::mt19937_64 g; explicit RNG(uint64_t s):g(s){}
    float uf(float a,float b){ std::uniform_real_distribution<float> d(a,b); return d(g);}
    int   ui(int a,int b){ std::uniform_int_distribution<int> d(a,b); return d(g);}
    bool  chance(float p){ std::bernoulli_distribution d(std::clamp(p,0.f,1.f)); return d(g);} };

inline std::vector<float> slope01(const std::vector<float>& h,int W,int H,float metersPer){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[idx(x,y,W)]; };
    float maxg=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y))*metersPer;
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1))*metersPer;
        float g=std::sqrt(gx*gx+gy*gy);
        s[idx(x,y,W)]=g; if(g>maxg) maxg=g;
    }
    for(float& v:s) v/=maxg;
    return s;
}

inline std::vector<uint8_t> derive_water(const std::vector<float>& h,int W,int H,float sea){
    std::vector<uint8_t> m((size_t)W*H,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x)
        m[idx(x,y,W)] = (h[idx(x,y,W)] < sea) ? 1u : 0u;
    return m;
}

// Multi-source distance to water (8-neigh Dijkstra)
inline std::vector<float> dist_to_water(const std::vector<uint8_t>& water, int W, int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;
    const int dx[8]={0,1,1,1,0,-1,-1,-1};
    const int dy[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1.f,1.4142f,1.f,1.4142f,1.f,1.4142f,1.f,1.4142f};

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=idx(x,y,W); if (water[i]){ d[i]=0.f; pq.emplace(0.f,(int)i); }
    }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop(); if (cd>d[i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            int j=(int)idx(nx,ny,W); float nd=cd+step[k];
            if (nd<d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

// Radial Gaussian around center for demand
inline float gaussian2(int x,int y, I2 c, float R, float sigmaRel){
    float dx=x-c.x, dy=y-c.y;
    float r = std::sqrt(dx*dx+dy*dy);
    float sigma = std::max(1.f, sigmaRel*R);
    return std::exp(-0.5f * (r*r)/(sigma*sigma));
}

// Pick K high-demand terminals with Poisson-like spacing
inline std::vector<I2> pick_terminals(const std::vector<float>& demand, int W,int H,
                                      int K, float minDist, RNG& rng)
{
    std::vector<I2> pts; pts.reserve(K);
    std::vector<int> order((size_t)W*H); for(size_t i=0;i<order.size();++i) order[i]=(int)i;
    std::sort(order.begin(), order.end(), [&](int a,int b){ return demand[(size_t)a]>demand[(size_t)b]; });

    auto far_enough=[&](int x,int y){
        for(const auto& p: pts){ float dx=x-p.x, dy=y-p.y; if (dx*dx+dy*dy < minDist*minDist) return false; }
        return true;
    };
    for(int idx_i : order){
        if ((int)pts.size()>=K) break;
        int x=idx_i%W, y=idx_i/W;
        if (demand[(size_t)idx_i] <= 0.01f) break;
        if (!far_enough(x,y)) continue;
        pts.push_back({x,y});
    }
    for(auto& p: pts){ p.x = std::clamp(p.x + rng.ui(-1,1), 0, W-1); p.y = std::clamp(p.y + rng.ui(-1,1), 0, H-1); }
    return pts;
}

// Dijkstra shortest path on grid with spatially varying per-cell cost
struct PathResult { std::vector<I2> polyline; float cost=0; bool ok=false; };

inline PathResult shortest_to_network(
    int sx,int sy,
    const std::vector<uint8_t>& network, // 1 where already part of the graph
    const std::vector<float>& cellCost,  // >=1 per-cell
    int W,int H)
{
    const size_t N=(size_t)W*H;
    using Node=std::pair<float,int>;
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;
    std::vector<float> dist(N, std::numeric_limits<float>::infinity());
    std::vector<int>   prev(N, -1);

    const int dx[8]={0,1,1,1,0,-1,-1,-1};
    const int dy[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1.f,1.4142f,1.f,1.4142f,1.f,1.4142f,1.f,1.4142f};

    int s=(int)idx(sx,sy,W);
    dist[(size_t)s]=0.f; pq.emplace(0.f,s);

    int hit=-1;
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop();
        if (cd>dist[(size_t)i]) continue;
        if (i!=s && network[(size_t)i]){ hit=i; break; }

        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            int j=(int)idx(nx,ny,W);
            float c = step[k] * cellCost[(size_t)j];
            float nd = cd + c;
            if (nd < dist[(size_t)j]){
                dist[(size_t)j]=nd; prev[(size_t)j]=i; pq.emplace(nd,j);
            }
        }
    }

    PathResult R; R.ok=false;
    if (hit<0) return R;
    R.ok=true; R.cost=dist[(size_t)hit];

    // backtrack
    std::vector<I2> rev; rev.reserve(256);
    for(int v=hit; v!=-1; v=prev[(size_t)v]) rev.push_back({v%W, v/W});
    std::reverse(rev.begin(), rev.end());
    R.polyline = std::move(rev);
    return R;
}

inline void rasterize_polyline_wide(const std::vector<I2>& P, int radius, int W,int H, std::vector<uint8_t>& mask){
    auto disc=[&](int cx,int cy){
        for(int oy=-radius; oy<=radius; ++oy)
        for(int ox=-radius; ox<=radius; ++ox){
            if (ox*ox+oy*oy > radius*radius) continue;
            int nx=cx+ox, ny=cy+oy; if(!inb(nx,ny,W,H)) continue;
            mask[idx(nx,ny,W)] = 1;
        }
    };
    for(size_t i=0;i<P.size();++i) disc(P[i].x, P[i].y);
    // link gaps (Bresenham)
    for(size_t i=1;i<P.size();++i){
        int x0=P[i-1].x, y0=P[i-1].y, x1=P[i].x, y1=P[i].y;
        int dx=std::abs(x1-x0), sx=x0<x1?1:-1;
        int dy=-std::abs(y1-y0), sy=y0<y1?1:-1;
        int err=dx+dy;
        int x=x0, y=y0;
        while(true){
            for(int oy=-radius; oy<=radius; ++oy)
            for(int ox=-radius; ox<=radius; ++ox){
                if (ox*ox+oy*oy > radius*radius) continue;
                int nx=x+ox, ny=y+oy; if(!inb(nx,ny,W,H)) continue;
                mask[idx(nx,ny,W)]=1;
            }
            if (x==x1 && y==y1) break;
            int e2=2*err;
            if (e2>=dy){ err+=dy; x+=sx; }
            if (e2<=dx){ err+=dx; y+=sy; }
        }
    }
}

// BFS flood fill into blocks (land & inside radius, excluding roads)
inline int flood_blocks(const std::vector<uint8_t>& buildable,
                        const std::vector<uint8_t>& road_mask,
                        int W,int H, std::vector<int>& out_block_id,
                        int min_area)
{
    out_block_id.assign((size_t)W*H, -1);
    int bid=0;
    std::vector<int> q; q.reserve(4096);

    const int dx4[4]={1,-1,0,0};
    const int dy4[4]={0,0,1,-1};

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if (!buildable[i] || road_mask[i] || out_block_id[i]!=-1) continue;

        q.clear(); q.push_back((int)i); out_block_id[i]=bid; int area=0;
        for(size_t qi=0; qi<q.size(); ++qi){
            int v=q[qi]; area++;
            int vx=v%W, vy=v/W;
            for(int k=0;k<4;++k){
                int nx=vx+dx4[k], ny=vy+dy4[k];
                if (!inb(nx,ny,W,H)) continue;
                size_t j=idx(nx,ny,W);
                if (buildable[j] && !road_mask[j] && out_block_id[j]==-1){
                    out_block_id[j]=bid; q.push_back((int)j);
                }
            }
        }
        if (area < min_area){
            for(size_t qi=0; qi<q.size(); ++qi) out_block_id[(size_t)q[qi]]=-1;
        } else {
            bid++;
        }
    }
    return bid;
}

// Poisson-like seed scatter within a block
inline std::vector<I2> scatter_in_block(const std::vector<int>& block_id, int W,int H,
                                        int block, float minSpacing, int want, RNG& rng){
    std::vector<I2> pts; pts.reserve(want);
    std::vector<int> cells;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x)
        if (block_id[idx(x,y,W)]==block) cells.push_back((int)idx(x,y,W));
    if (cells.empty()) return pts;

    std::shuffle(cells.begin(), cells.end(), rng.g);

    auto farEnough=[&](int x,int y){
        for(const auto& p: pts){ float dx=x-p.x, dy=y-p.y; if (dx*dx+dy*dy < minSpacing*minSpacing) return false; }
        return true;
    };

    for(int v : cells){
        int x=v%W, y=v/W;
        if (farEnough(x,y)){ pts.push_back({x,y}); if ((int)pts.size()>=want) break; }
    }
    return pts;
}

// Assign each cell in block to nearest seed (grid-Voronoi)
inline void assign_voronoi_block(const std::vector<int>& block_id, int W,int H, int block,
                                  const std::vector<I2>& seeds, std::vector<int>& out_ids,
                                  int parcel_base_id)
{
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if (block_id[i]!=block){ out_ids[i]=-1; continue; }
        int best=-1; float bestd=1e30f;
        for(size_t s=0;s<seeds.size();++s){
            float dx=x-seeds[s].x, dy=y-seeds[s].y;
            float d=dx*dx+dy*dy;
            if (d<bestd){ bestd=d; best=(int)s; }
        }
        out_ids[i] = (best>=0) ? (parcel_base_id + best) : -1;
    }
}

// One Lloyd/CVT iteration inside this block
inline void lloyd_once(const std::vector<int>& block_id, int W,int H, int block,
                       std::vector<I2>& seeds, const std::vector<int>& parcel_ids,
                       int parcel_base_id)
{
    const int S=(int)seeds.size();
    if (S==0) return;
    std::vector<double> sumx(S,0.0), sumy(S,0.0); std::vector<int> cnt(S,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if (block_id[i]!=block) continue;
        int pid = parcel_ids[i] - parcel_base_id;
        if (pid<0 || pid>=S) continue;
        sumx[(size_t)pid]+=x; sumy[(size_t)pid]+=y; cnt[(size_t)pid]++;
    }
    for(int s=0;s<S;++s) if (cnt[(size_t)s]>0){
        seeds[(size_t)s].x=(int)std::round(sumx[(size_t)s]/cnt[(size_t)s]);
        seeds[(size_t)s].y=(int)std::round(sumy[(size_t)s]/cnt[(size_t)s]);
    }
}

} // namespace detail

// --------------------------- Entry point ---------------------------

inline TownLayout GenerateTownLayout(const std::vector<float>& height01,
                                     int W,int H,
                                     const TownParams& P,
                                     const std::vector<uint8_t>* waterMaskOpt /*W*H, 1=water*/ = nullptr,
                                     const std::vector<I2>* portals /*optional world-road entrances*/ = nullptr)
{
    TownLayout out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return out;

    detail::RNG rng(P.seed);

    // Basic fields
    auto slope = detail::slope01(height01, W, H, P.meters_per_height_unit);
    std::vector<uint8_t> water = waterMaskOpt ? *waterMaskOpt
                                              : detail::derive_water(height01, W, H, P.sea_level);
    auto d2w = detail::dist_to_water(water, W, H);

    // Buildable mask: inside city disc & on land
    std::vector<uint8_t> buildable(N,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        float dx=x-P.center.x, dy=y-P.center.y;
        if (dx*dx+dy*dy <= P.city_radius*P.city_radius && !water[i]) buildable[i]=1;
    }

    // Demand field
    std::vector<float> demand(N,0.0f);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W); if (!buildable[i]) continue;
        float g   = detail::gaussian2(x,y, P.center, P.city_radius, P.demand_sigma);
        float wet = 1.0f - std::min(1.0f, d2w[i] / (0.30f*P.city_radius + 1e-3f));
        float flat= 1.0f - slope[i];
        demand[i] = detail::clamp01( 0.6f*g + P.water_attract*wet + (1.0f-P.slope_avoid)*flat );
    }

    // Terminals to connect
    auto T = detail::pick_terminals(demand, W, H, P.terminals, P.terminal_min_spacing, rng);
    if (portals){ for(auto p : *portals){ if (detail::inb(p.x,p.y,W,H)) T.push_back(p); } }

    // Per-cell movement cost (weighted shortest paths)
    std::vector<float> cellCost(N, 4.0f); // base
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::idx(x,y,W);
        float c = 1.0f + P.slope_cost * slope[i];
        if (water[i]) c = P.water_cost; // forbid
        if (!buildable[i]) c *= 4.0f;   // discourage outside town disc
        cellCost[i] = c;
    }

    // Grow network: start from center as initial network
    std::vector<uint8_t> network(N,0);
    network[(size_t)detail::idx(P.center.x,P.center.y,W)] = 1;

    out.road_mask.assign(N, 0);
    out.block_id.assign(N, -1);
    out.parcel_id.assign(N, -1);

    // Connect terminals, highest-demand first
    std::sort(T.begin(), T.end(), [&](const I2& a,const I2& b){
        return demand[(size_t)detail::idx(a.x,a.y,W)] > demand[(size_t)detail::idx(b.x,b.y,W)];
    });

    for(const auto& t : T){
        auto path = detail::shortest_to_network(t.x,t.y, network, cellCost, W,H);
        if (!path.ok || path.polyline.size()<2) continue;
        out.roads.push_back(path.polyline);
        // mark into network
        for(const auto& p : path.polyline) network[(size_t)detail::idx(p.x,p.y,W)]=1;
        // rasterize widened road
        detail::rasterize_polyline_wide(path.polyline, std::max(1, P.road_width), W,H, out.road_mask);
    }

    // Flood-fill buildable land into blocks, skipping roads
    out.blocks = detail::flood_blocks(buildable, out.road_mask, W,H, out.block_id, P.block_min_area);

    // --- Parcelization by block (Voronoi + Lloyd) ---
    int nextParcelId = 0;
    std::vector<int> temp_parcel(N, -1);

    for(int b=0; b<out.blocks; ++b){
        // Count area and estimate desired seeds
        int area=0;
        for(size_t i=0;i<N;++i) if (out.block_id[i]==b) area++;
        if (area <= 0) continue;

        int want = std::max(1, (int)std::round(area / std::max(10.0f, P.target_parcel_area)));
        auto seeds = detail::scatter_in_block(out.block_id, W,H, b, P.parcel_min_spacing, want, rng);
        if (seeds.empty()) continue;

        // Assign → optional CVT relax → assign
        detail::assign_voronoi_block(out.block_id, W,H, b, seeds, temp_parcel, nextParcelId);
        for(int it=0; it<P.lloyd_iters; ++it){
            detail::lloyd_once(out.block_id, W,H, b, seeds, temp_parcel, nextParcelId);
            detail::assign_voronoi_block(out.block_id, W,H, b, seeds, temp_parcel, nextParcelId);
        }

        // Stamp into global parcel_id
        for(size_t i=0;i<N;++i) if (out.block_id[i]==b) out.parcel_id[i]=temp_parcel[i];

        nextParcelId += (int)seeds.size();
    }
    out.parcels = nextParcelId;

    return out;
}

/*
----------------------------------- Usage -----------------------------------

#include "worldgen/TownLayoutGenerator.hpp"

void makeTown(const std::vector<float>& height01, int W, int H,
              const std::vector<uint8_t>* waterMask,
              const std::vector<worldgen::I2>* portals /*optional*/)
{
    worldgen::TownParams P;
    P.width=W; P.height=H;
    P.center = { W/2, H/2 };
    P.city_radius = 180.0f;
    P.terminals = 24;
    P.road_width = 2;
    P.target_parcel_area = 90.0f;

    worldgen::TownLayout T = worldgen::GenerateTownLayout(height01, W, H, P, waterMask, portals);

    // Hook into the game:
    //  • Render T.road_mask (decals/meshes) and draw T.roads polylines.
    //  • Use T.block_id to place plazas/markets per block centroid.
    //  • Sample each parcel (T.parcel_id) to place buildings with frontage rules.
}
*/
} // namespace worldgen
