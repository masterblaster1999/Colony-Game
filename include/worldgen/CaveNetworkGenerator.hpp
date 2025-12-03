#pragma once
// ============================================================================
// CaveNetworkGenerator.hpp — karst-like caves, sinkholes, and entrances
// For Colony-Game | C++17 / STL-only | No dependencies
//
// What it produces on a W×H grid (same size as your height map):
//  • cave_mask[z]      : vector<uint8_t> per depth layer (0=none,1=cave)
//  • cave_paths[z]     : vector of polylines (grid coords) per depth layer
//  • entrance_points   : surface openings (x,y) where caves reach the surface
//  • karst_potential01 : 0..1 field (debug) showing where caves are likely
//
// Why these cues?
//  • Solution caves and sinkholes cluster in soluble rock terrains (karst), and
//    passages often follow fractures/joints and valleys; sinkholes can form by
//    dissolution or roof collapse. We bias tunnels toward concave terrain and
//    stream corridors to emulate that pattern (concepts: USGS/NPS/Britannica).
//    -- USGS karst/sinkholes overview
//    -- NPS solution-cave passages along joints/bedding & layers
//    -- Britannica: joint- vs bedding-plane–controlled passages; sinkholes
// ============================================================================

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <random>
#include <queue>
#include <utility>

namespace worldgen {

struct CaveParams {
    int   width = 0, height = 0;
    float sea_level = 0.50f;          // height01 <= sea → water
    int   depth_layers = 3;           // 1..3; shallow=0, mid=1, deep=2

    // Seeding & growth
    int   seed_count = 140;           // starting sources for "worms"
    int   seed_min_spacing = 18;      // cells between seeds
    int   max_steps_per_worm = 400;   // cap per worm
    float branch_prob = 0.07f;        // chance to bifurcate at a step
    float stop_prob   = 0.003f;       // chance to stop at a step
    float curvature   = 0.55f;        // 0=straight, 1=random turns
    float valley_bias = 0.55f;        // follow concavities
    float downslope_bias = 0.35f;     // drift along -∇height
    float river_bias  = 0.35f;        // pull toward rivers (if flow_accum provided)
    float lake_avoid  = 0.50f;        // repel from lake interiors

    // Entrance detection
    float entrance_slope_min = 0.18f; // slope01 threshold for openings (cliffs/steep)
    int   entrance_min_spacing = 22;  // blue-noise spacing between entrances (cells)

    // Carve width (cells) by "strength" (valley & flow). Width grows with stream size.
    float base_radius = 1.2f;         // min half-width (cells)
    float extra_radius = 2.4f;        // added half-width with high strength

    // RNG
    uint64_t seed = 0xCAV3CAV3u;
};

// One polyline per cave segment
struct CavePolyline {
    std::vector<std::pair<int,int>> points;
    int   layer = 0;      // 0..depth_layers-1
};

struct CaveResult {
    int width=0, height=0;
    std::vector<std::vector<uint8_t>> cave_mask; // [layer][W*H]
    std::vector<CavePolyline> cave_paths;        // all polylines w/ layer tags
    std::vector<std::pair<int,int>> entrances;   // surface (x,y)
    std::vector<float> karst_potential01;        // W*H (debug)
    std::vector<float> slope01;                  // W*H (debug)
};

// ------------------------------- internals ---------------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

struct RNG { std::mt19937_64 g; explicit RNG(uint64_t s):g(s){} 
             float uf(){ return std::uniform_real_distribution<float>(0.f,1.f)(g);} 
             int ui(int a,int b){ return std::uniform_int_distribution<int>(a,b)(g);} };

inline void gradient_slope(const std::vector<float>& h,int W,int H,
                           std::vector<float>& gx,std::vector<float>& gy,std::vector<float>& slope01)
{
    gx.assign((size_t)W*H,0.f); gy.assign((size_t)W*H,0.f); slope01.assign((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float Gx = 0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float Gy = 0.5f*(Hs(x,y+1)-Hs(x,y-1));
        gx[I(x,y,W)]=Gx; gy[I(x,y,W)]=Gy;
        float mag = std::sqrt(Gx*Gx + Gy*Gy);
        slope01[I(x,y,W)]=mag; gmax=std::max(gmax,mag);
    }
    for(float& v:slope01) v/=gmax;
}

inline std::vector<float> laplacian_valley(const std::vector<float>& h,int W,int H){
    std::vector<float> v((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float mn=1e9f, mx=-1e9f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float c=Hs(x,y);
        float nb = (Hs(x-1,y)+Hs(x+1,y)+Hs(x,y-1)+Hs(x,y+1)
                   +Hs(x-1,y-1)+Hs(x+1,y-1)+Hs(x-1,y+1)+Hs(x+1,y+1))/8.f;
        float lap = nb - c; // positive if center is lower than neighbors (concavity)
        v[I(x,y,W)] = std::max(0.f, lap);
        mn=std::min(mn, v[I(x,y,W)]); mx=std::max(mx, v[I(x,y,W)]);
    }
    float rg=std::max(1e-6f, mx-mn);
    for(float& f:v) f=(f-mn)/rg;
    return v;
}

// normalize arbitrary scalar field (0..1)
inline void normalize(std::vector<float>& a){
    float mn=1e9f, mx=-1e9f; for(float v:a){ mn=std::min(mn,v); mx=std::max(mx,v); }
    float rg=std::max(1e-6f, mx-mn); for(float& v:a) v=(v-mn)/rg;
}

// distance to mask (8-neigh), cheap BFS int distance
inline std::vector<int> dist8_to_mask(const std::vector<uint8_t>& mask,int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<int> d(N, INT_MAX);
    std::queue<int> q;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W); if (mask[i]){ d[i]=0; q.push((int)i); }
    }
    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    while(!q.empty()){
        int v=q.front(); q.pop();
        int x=v%W, y=v/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k];
            if(!inb(nx,ny,W,H)) continue;
            size_t j=I(nx,ny,W);
            if (d[j] > d[(size_t)v]+1){ d[j]=d[(size_t)v]+1; q.push((int)j); }
        }
    }
    return d;
}

inline void stamp_disc(std::vector<uint8_t>& grid,int W,int H,int cx,int cy,float radius){
    int R=(int)std::ceil(radius);
    for(int oy=-R; oy<=R; ++oy){
        for(int ox=-R; ox<=R; ++ox){
            int nx=cx+ox, ny=cy+oy; if(!inb(nx,ny,W,H)) continue;
            float d2= (float)(ox*ox + oy*oy);
            if (d2 <= radius*radius) grid[I(nx,ny,W)] = 1u;
        }
    }
}

inline float lerp(float a,float b,float t){ return a + (b-a)*t; }

} // namespace detail

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------
inline CaveResult GenerateCaves(
    const std::vector<float>& height01, int W,int H,
    const CaveParams& P = {},
    // Optional hydrology
    const std::vector<uint32_t>* flow_accum /*W*H*/ = nullptr,
    const std::vector<uint8_t>*  lake_mask  /*W*H*/ = nullptr)
{
    CaveResult out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || (int)height01.size()!=W*H) return out;

    // 1) Terrain primitives: gradient, slope, concavity (valley index)
    std::vector<float> gx, gy; 
    detail::gradient_slope(height01, W,H, gx, gy, out.slope01);
    std::vector<float> valley = detail::laplacian_valley(height01, W,H);

    // 2) Hydrology helpers (optional)
    std::vector<float> flow01(N,0.f);
    if (flow_accum){
        uint32_t amin=UINT32_MAX, amax=0;
        for(size_t i=0;i<N;++i){ amin=std::min(amin, (*flow_accum)[i]); amax=std::max(amax, (*flow_accum)[i]); }
        float rg = (amax>amin)? float(amax-amin) : 1.f;
        for(size_t i=0;i<N;++i){ flow01[i] = std::sqrt(std::max(0.f, ((*flow_accum)[i]-amin)/rg)); }
        detail::normalize(flow01);
    }
    std::vector<int> d2lake;
    if (lake_mask && !lake_mask->empty()) d2lake = detail::dist8_to_mask(*lake_mask, W,H);

    // 3) Karst potential ~ valleys + near rivers − lake interiors − steep cliffs far from water
    out.karst_potential01.assign(N, 0.f);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);
        float e  = std::max(0.f, height01[i]-P.sea_level) / std::max(1e-6f, 1.f-P.sea_level);
        float nearLake = (!d2lake.empty()? std::exp( - (d2lake[i]*d2lake[i]) / (50.f) ) : 0.f);
        float k = 0.60f*valley[i]
                + 0.35f*(flow_accum? flow01[i] : 0.f)
                + 0.15f*nearLake
                + 0.10f*(1.0f - e)           // lowlands favored
                - 0.10f*out.slope01[i];      // avoid sheer cliffs (except for entrances)
        out.karst_potential01[i] = detail::clamp01(k);
    }

    // 4) Pick seed cells in high-potential zones with blue-noise spacing
    std::vector<int> order((size_t)W*H);
    for(size_t i=0;i<N;++i) order[i]=(int)i;
    // sort by potential descending
    std::sort(order.begin(), order.end(), [&](int a,int b){
        return out.karst_potential01[(size_t)a] > out.karst_potential01[(size_t)b];
    });

    std::vector<uint8_t> seedMask(N,0);
    std::vector<std::pair<int,int>> seeds;
    auto far_enough = [&](int x,int y){
        int R=P.seed_min_spacing;
        for (auto [sx,sy] : seeds) if (std::abs(sx-x)<=R && std::abs(sy-y)<=R) return false;
        return true;
    };
    for(int idx : order){
        if ((int)seeds.size() >= P.seed_count) break;
        if (out.karst_potential01[(size_t)idx] < 0.35f) break;
        int x=idx%W, y=idx/W;
        if (far_enough(x,y)){ seeds.push_back({x,y}); seedMask[(size_t)idx]=1; }
    }

    // 5) Allocate cave masks
    int Z = std::clamp(P.depth_layers, 1, 3);
    out.cave_mask.assign(Z, std::vector<uint8_t>((size_t)W*H, 0u));

    // 6) Grow "worms" biased by valley, downslope, flow; carve radius by strength
    detail::RNG rng(P.seed);
    static const int dx8[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy8[8]={0,1,1,1,0,-1,-1,-1};

    auto strengthAt=[&](int x,int y){
        size_t i=detail::I(x,y,W);
        float s = 0.65f*valley[i] + 0.35f*(flow_accum? flow01[i] : 0.f);
        return detail::clamp01(s);
    };

    for(auto [sx,sy] : seeds){
        // pick depth layer: valley/flow favor shallow; randomize a bit
        float s0 = strengthAt(sx,sy);
        int layer = (Z==1)?0 : (s0>0.6f? 0 : (s0>0.35f? 1 : 2));
        int x=sx, y=sy;
        // initial heading points roughly downslope
        float hx = -gx[detail::I(x,y,W)], hy = -gy[detail::I(x,y,W)];
        float hn = std::sqrt(hx*hx+hy*hy); if (hn<1e-6f){ hx=1.f; hy=0.f; } else { hx/=hn; hy/=hn; }

        CavePolyline poly; poly.layer=layer; poly.points.reserve(128);
        int steps=0;
        while(steps++ < P.max_steps_per_worm){
            poly.points.push_back({x,y});

            // carve a disk width based on local "strength"
            float st = strengthAt(x,y);
            float radius = P.base_radius + P.extra_radius * st;
            detail::stamp_disc(out.cave_mask[layer], W,H, x,y, radius);

            // bias toward valleys/downslope/streams; repel lake interiors
            float bx=0.f, by=0.f;

            // valley gradient approx: move toward increasing valley (finite diff)
            auto val = [&](int _x,int _y){
                _x=std::clamp(_x,0,W-1); _y=std::clamp(_y,0,H-1);
                return valley[detail::I(_x,_y,W)];
            };
            float vx = 0.5f*(val(x+1,y)-val(x-1,y));
            float vy = 0.5f*(val(x,y+1)-val(x,y-1));
            bx += P.valley_bias * vx; by += P.valley_bias * vy;

            // downslope bias
            bx += P.downslope_bias * (-gx[detail::I(x,y,W)]);
            by += P.downslope_bias * (-gy[detail::I(x,y,W)]);

            // river attraction
            if (flow_accum){
                auto fv = [&](int _x,int _y){
                    _x=std::clamp(_x,0,W-1); _y=std::clamp(_y,0,H-1);
                    return flow01[detail::I(_x,_y,W)];
                };
                float rx = 0.5f*(fv(x+1,y)-fv(x-1,y));
                float ry = 0.5f*(fv(x,y+1)-fv(x,y-1));
                bx += P.river_bias * rx; by += P.river_bias * ry;
            }

            // lake repulsion
            if (!d2lake.empty()){
                float near= std::exp( - (d2lake[detail::I(x,y,W)]*d2lake[detail::I(x,y,W)]) / 25.f );
                // push outward along distance gradient (approx by finite diff)
                auto dd=[&](int _x,int _y){ _x=std::clamp(_x,0,W-1); _y=std::clamp(_y,0,H-1); 
                                            return (float)d2lake[detail::I(_x,_y,W)]; };
                float lx = 0.5f*(dd(x+1,y)-dd(x-1,y));
                float ly = 0.5f*(dd(x,y+1)-dd(x,y-1));
                bx -= P.lake_avoid * near * lx;
                by -= P.lake_avoid * near * ly;
            }

            // normalize and add curvature noise
            float bn = std::sqrt(bx*bx+by*by); if (bn>1e-6f){ bx/=bn; by/=bn; }
            // blend with current heading (inertia) and a small random turn
            float ang = (rng.uf()*2.f-1.f) * (P.curvature * 3.14159265f/4.f);
            float ca=std::cos(ang), sa=std::sin(ang);
            float hx2 = detail::clamp01(0.001f + (0.65f*hx + 0.35f*bx));
            float hy2 = detail::clamp01(0.001f + (0.65f*hy + 0.35f*by));
            // rotate by noise
            float nx = hx2*ca - hy2*sa, ny = hx2*sa + hy2*ca;
            // pick the best of 8 neighbors aligned with (nx,ny) and higher strength
            int bestk=0; float bestScore=-1e9f;
            for(int k=0;k<8;++k){
                int nxg=x+dx8[k], nyg=y+dy8[k]; if(!detail::inb(nxg,nyg,W,H)) continue;
                float align = (dx8[k]*nx + dy8[k]*ny); // directional dot
                float st2 = strengthAt(nxg,nyg);
                float sc = 0.7f*align + 0.3f*st2;
                if (sc>bestScore){ bestScore=sc; bestk=k; }
            }
            x += dx8[bestk]; y += dy8[bestk];

            // stop conditions
            if (rng.uf() < P.stop_prob) break;
            if (x<=1||y<=1||x>=W-2||y>=H-2) break;

            // occasional branch
            if (rng.uf() < P.branch_prob){
                seeds.push_back({x,y}); // enqueue as a new seed (same layer)
            }
        }
        if (poly.points.size()>=2) out.cave_paths.push_back(std::move(poly));
    }

    // 7) Surface entrances: find cave cells adjacent to steep slopes or cutting surface
    //    Near valleys/river banks and breaklines we expose an "opening".
    std::vector<uint8_t> entranceMask(N,0);
    auto mark_if_open=[&](int x,int y){
        size_t i=detail::I(x,y,W);
        if (entranceMask[i]) return;
        if (out.slope01[i] >= P.entrance_slope_min){
            entranceMask[i]=1;
            out.entrances.push_back({x,y});
        }
    };
    // any shallow-layer cave cell next to non-cave & steep slope → entrance
    if (!out.cave_mask.empty()){
        const auto& shallow = out.cave_mask[0];
        for(int y=1;y<H-1;++y) for(int x=1;x<W-1;++x){
            size_t i=detail::I(x,y,W);
            if (!shallow[i]) continue;
            // look for a neighboring non-cave with steep slope
            bool edge=false;
            for(int oy=-1;oy<=1 && !edge;++oy) for(int ox=-1;ox<=1 && !edge;++ox){
                if (ox==0 && oy==0) continue;
                int nx=x+ox, ny=y+oy; size_t j=detail::I(nx,ny,W);
                if (!shallow[j] && out.slope01[j] >= P.entrance_slope_min) edge=true;
            }
            if (edge) mark_if_open(x,y);
        }
    }
    // space entrances (blue-noise): keep farthest sampling
    std::vector<std::pair<int,int>> spaced;
    auto farEnough=[&](int x,int y){
        for (auto [ax,ay] : spaced){
            if (std::abs(ax-x) <= P.entrance_min_spacing && std::abs(ay-y) <= P.entrance_min_spacing)
                return false;
        }
        return true;
    };
    // sort entrances by local strength (river/valley) so strong ones survive
    std::sort(out.entrances.begin(), out.entrances.end(), [&](auto a, auto b){
        float sa = strengthAt(a.first,a.second), sb = strengthAt(b.first,b.second);
        return sa > sb;
    });
    for (auto p : out.entrances){ if (farEnough(p.first,p.second)) spaced.push_back(p); }
    out.entrances.swap(spaced);

    return out;
}

/*
-------------------------------- Usage --------------------------------

#include "worldgen/CaveNetworkGenerator.hpp"

// If you have hydrology (flow_accum) and lake_mask from your Hydro generator,
// pass them in for smarter placement; otherwise, they are optional.

void build_caves(const std::vector<float>& height01, int W,int H,
                 const std::vector<uint32_t>* flow_accum, // optional
                 const std::vector<uint8_t>*  lake_mask)  // optional
{
    worldgen::CaveParams P;
    P.width=W; P.height=H;
    P.seed_count = 140;
    P.depth_layers = 3;     // shallow/mid/deep
    P.base_radius = 1.0f;   // tune with your world scale
    P.extra_radius = 2.6f;

    worldgen::CaveResult C = worldgen::GenerateCaves(height01, W,H, P, flow_accum, lake_mask);

    // Hooks:
    //  • Render shallow layer caves as entrances & small grottos; stitch mid/deep as tunnels.
    //  • Spawn ore pockets or rare fauna only inside caves.
    //  • Place hazards: collapses near steep openings; moisture-loving flora near cave mouths.
    //  • Use C.karst_potential01 as a debug heatmap to tune seed_count/branch_prob.
}
*/
} // namespace worldgen
