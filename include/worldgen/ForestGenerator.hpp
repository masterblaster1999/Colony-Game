#pragma once
// ============================================================================
// ForestGenerator.hpp — moisture/energy-based forest classification
// + blue-noise tree placement for Colony-Game (C++17 / STL-only)
//
// What it computes on a W×H grid:
//  • slope01, aspect_rad                     — from height01 (debug/useful signals)
//  • d2water                                 — 8-neigh distance (cells) to water
//  • moisture01, energy01                    — proxies for wetness/heat
//  • forest_type (per cell)                  — NONE/RIPARIAN/DECIDUOUS/MIXED/CONIFER/SCRUB
//  • canopy01 (per cell)                     — continuous canopy density 0..1
//  • tree instances (x,y,species_id)         — Poisson-disk (blue-noise) points
//
// Why these signals:
//  • Riparian zones pack dense vegetation along rivers & streams. [USGS]           (cit.)
//  • North-/east-facing slopes in NH are generally cooler & moister,               (cit.)
//    south-/west-facing warmer & drier; aspect modulates forest makeup. [USFS]
//  • Poisson-disk spacing yields natural, non-griddy distributions. [Bridson]      (cit.)
//
// References (concepts; implementation here is original):
//  • USGS riparian vegetation overview (2025).         (see README)
//  • USDA Forest Service notes & studies on aspect.    (see README)
//  • Bridson, "Fast Poisson Disk Sampling" (SIGGRAPH07).
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>

namespace worldgen {

enum ForestType : uint8_t {
    FT_NONE = 0, FT_RIPARIAN = 1, FT_DECIDUOUS = 2, FT_MIXED = 3, FT_CONIFER = 4, FT_SCRUB = 5
};

struct TreeInstance {
    int x=0, y=0;
    uint16_t species_id=0; // your renderer/DB can map ids to meshes (e.g., 0=oak,1=birch,2=willow,3=pine,...)
};

struct ForestParams {
    int   width = 0, height = 0;
    float sea_level = 0.50f;                 // height01 <= sea → water (if waterMask is null)
    bool  north_hemisphere = true;           // flip aspect polarity for SH worlds
    float meters_per_height_unit = 1200.0f;  // for slope normalization (debug/relative only)

    // Moisture model
    float riparian_radius_cells = 18.0f;     // distance where water influence fades
    float slope_dryness = 0.35f;             // steeper ground sheds water → drier
    float elevation_dryness = 0.15f;         // higher above sea → drier (coarse proxy)

    // Energy (heat) model
    float elevation_cooling = 0.45f;         // cooler at higher elevation (proxy)
    float aspect_cooling    = 0.20f;         // NH: north-facing cooler (SH flips)

    // Classification thresholds (tune to taste)
    float riparian_moist_min = 0.70f;        // ≥ → force RIPARIAN
    float forest_canopy_min  = 0.22f;        // < → classify as SCRUB/NONE
    float conifer_cool_max   = 0.35f;        // energy01 ≤ → CONIFER if moist enough
    float decid_warm_min     = 0.55f;        // energy01 ≥ → DECIDUOUS if moist enough

    // Canopy density shaping
    float canopy_from_moist  = 0.70f;        // canopy ≈ a*moist + b*(1-slope)
    float canopy_from_flat   = 0.30f;

    // Poisson sampling per forest type (cell radii)
    float r_riparian   = 3.3f;
    float r_deciduous  = 3.8f;
    float r_mixed      = 4.1f;
    float r_conifer    = 4.3f;
    float r_scrub      = 6.0f;

    // Output density & caps
    float instance_density = 1.0f;           // multiplier for counts (1.0 = default)
    int   max_instances    = 250000;         // safety cap for huge maps

    uint64_t seed = 0x6D2B79F5u;
};

struct ForestResult {
    int width=0, height=0;

    std::vector<float>   slope01;       // size W*H
    std::vector<float>   aspect_rad;    // [-pi..pi], 0=east, +pi/2=north
    std::vector<float>   d2water;       // distance (cells) to nearest water
    std::vector<float>   moisture01;    // 0..1
    std::vector<float>   energy01;      // 0..1 (warmth proxy)
    std::vector<uint8_t> forest_type;   // ForestType per cell
    std::vector<float>   canopy01;      // 0..1
    std::vector<TreeInstance> trees;    // instances
};

// --------------------------- internals ---------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

inline std::vector<uint8_t> derive_water(const std::vector<float>& h,int W,int H,float sea){
    std::vector<uint8_t> m((size_t)W*H,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x)
        m[I(x,y,W)] = (h[I(x,y,W)] <= sea) ? 1u : 0u;
    return m;
}

inline void slope_aspect(const std::vector<float>& h,int W,int H,float metersPer,
                         std::vector<float>& slope01, std::vector<float>& aspect)
{
    slope01.assign((size_t)W*H,0.f);
    aspect.assign((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y))*metersPer;
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1))*metersPer;
        float g=std::sqrt(gx*gx+gy*gy);
        slope01[I(x,y,W)]=g; aspect[I(x,y,W)]=std::atan2(gy,gx); gmax=std::max(gmax,g);
    }
    for(float& v : slope01) v/=gmax;
}

// multi-source 8-neigh Dijkstra distance to mask (1=candidate)
inline std::vector<float> dist_to_mask(const std::vector<uint8_t>& src,int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;
    const int dx[8]={0,1,1,1,0,-1,-1,-1};
    const int dy[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1.f,1.4142f,1.f,1.4142f,1.f,1.4142f,1.f,1.4142f};

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W); if (src[i]){ d[i]=0.f; pq.emplace(0.f,(int)i); }
    }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop(); if (cd>d[i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            int j=(int)I(nx,ny,W);
            float nd=cd+step[k];
            if (nd<d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

// simple RNG helper
struct RNG { std::mt19937_64 g; explicit RNG(uint64_t s):g(s){} float uf(){ return std::uniform_real_distribution<float>(0.f,1.f)(g);} };

// Poisson-disk sampler (constant radius) over a boolean mask using grid binning.
// Not Bridson's full algorithm, but blue-noise-ish and fast for terrain grids.
inline std::vector<TreeInstance> poisson_over_mask(const std::vector<uint8_t>& allowed,
                                                   int W,int H, float r_cells, int species_id,
                                                   int cap, RNG& rng)
{
    const int R=(int)std::ceil(r_cells);
    std::vector<TreeInstance> pts; pts.reserve(std::min(cap, W*H/((R*R)+1)));
    std::vector<uint8_t> occ((size_t)W*H, 0);

    // randomized scan to avoid directional bias
    std::vector<int> order((size_t)W*H); for(size_t i=0;i<order.size();++i) order[i]=(int)i;
    std::shuffle(order.begin(), order.end(), rng.g);

    for(int v : order){
        if (cap>0 && (int)pts.size()>=cap) break;
        if (!allowed[(size_t)v]) continue;
        int x=v%W, y=v/W; bool ok=true;
        for(int oy=-R;oy<=R && ok;++oy){
            for(int ox=-R;ox<=R;++ox){
                int nx=x+ox, ny=y+oy; if(!inb(nx,ny,W,H)) continue;
                float d2=(float)(ox*ox+oy*oy);
                if (d2 <= r_cells*r_cells && occ[(size_t)I(nx,ny,W)]){ ok=false; break; }
            }
        }
        if(!ok) continue;
        occ[(size_t)I(x,y,W)]=1;
        pts.push_back(TreeInstance{x,y,(uint16_t)species_id});
    }
    return pts;
}

} // namespace detail

// --------------------------- main API ---------------------------

inline ForestResult GenerateForest(
    const std::vector<float>& height01, int W,int H,
    const ForestParams& P = {},
    const std::vector<uint8_t>* waterMaskOpt /*W*H, 1=water*/ = nullptr)
{
    ForestResult R; R.width=W; R.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return R;

    // 1) base fields
    std::vector<uint8_t> water = waterMaskOpt ? *waterMaskOpt : detail::derive_water(height01, W,H, P.sea_level);
    detail::slope_aspect(height01, W,H, P.meters_per_height_unit, R.slope01, R.aspect_rad);
    R.d2water = detail::dist_to_mask(water, W,H);

    // 2) moisture & energy proxies
    R.moisture01.assign(N,0.f);
    R.energy01.assign(N,0.f);

    // normalized elevation above sea (0..1 over land)
    std::vector<float> elev01(N,0.f);
    for(size_t i=0;i<N;++i){
        float e = std::max(0.f, height01[i] - P.sea_level) / std::max(1e-6f, 1.f - P.sea_level);
        elev01[i] = detail::clamp01(e);
    }

    // aspect modifier: NH north-facing (~+pi/2) is cooler; SH flips sign
    const float sign = P.north_hemisphere ? +1.f : -1.f;

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);

        // moisture: close to water + flatter + lower elevation → wetter
        float nearW = std::exp( - (R.d2water[i]*R.d2water[i]) / (P.riparian_radius_cells*P.riparian_radius_cells + 1e-6f) );
        float flat  = 1.f - R.slope01[i];
        float low   = 1.f - elev01[i];
        float moist = 0.60f*nearW + P.slope_dryness*flat + P.elevation_dryness*low;
        R.moisture01[i] = detail::clamp01(moist);

        // energy: lower at high elevation; aspect cools north-facing (NH)
        float aspectCool = 0.5f*(1.f + std::sin(R.aspect_rad[i] * sign)); // ~1 at north (NH), ~0 at south
        float energy = 1.f - P.elevation_cooling*elev01[i] - P.aspect_cooling*aspectCool;
        R.energy01[i] = detail::clamp01(energy);
    }

    // 3) classify forest type
    R.forest_type.assign(N, FT_NONE);
    R.canopy01.assign(N, 0.f);

    for(size_t i=0;i<N;++i){
        if (water[i]){ R.forest_type[i]=FT_NONE; continue; }

        float moist = R.moisture01[i];
        float ener  = R.energy01[i];
        float canopy = detail::clamp01(P.canopy_from_moist*moist + P.canopy_from_flat*(1.f - R.slope01[i]));

        uint8_t type = FT_SCRUB;
        if (moist >= P.riparian_moist_min){ type = FT_RIPARIAN; }
        else if (canopy < P.forest_canopy_min){ type = FT_SCRUB; }
        else {
            if (ener <= P.conifer_cool_max)       type = FT_CONIFER;
            else if (ener >= P.decid_warm_min)    type = FT_DECIDUOUS;
            else                                   type = FT_MIXED;
        }

        R.forest_type[i] = type;
        R.canopy01[i]    = canopy;
    }

    // 4) Poisson-disk tree instances per type (constant radii per type)
    //    Build allowed masks per type and sample separately; cap total instances.
    std::vector<uint8_t> allow_rip(N,0), allow_dec(N,0), allow_mix(N,0), allow_con(N,0), allow_scr(N,0);
    for(size_t i=0;i<N;++i){
        if (R.forest_type[i]==FT_RIPARIAN)   allow_rip[i] = 1;
        else if (R.forest_type[i]==FT_DECIDUOUS) allow_dec[i] = 1;
        else if (R.forest_type[i]==FT_MIXED)     allow_mix[i] = 1;
        else if (R.forest_type[i]==FT_CONIFER)   allow_con[i] = 1;
        else if (R.forest_type[i]==FT_SCRUB)     allow_scr[i] = 1;
    }

    detail::RNG rng(P.seed);

    auto add_pts = [&](const std::vector<uint8_t>& mask, float r, int speciesId, float density){
        int cap = (int)std::round(P.max_instances * density);
        auto pts = detail::poisson_over_mask(mask, W,H, r, speciesId, cap, rng);
        R.trees.insert(R.trees.end(), pts.begin(), pts.end());
    };

    // Simple species palette mapping (override in your game data if desired):
    //  0=willow (riparian), 1=oak (deciduous), 2=birch (mixed), 3=pine (conifer), 4=juniper (scrub)
    add_pts(allow_rip, P.r_riparian, 0, 1.10f * P.instance_density);
    add_pts(allow_dec, P.r_deciduous,1, 1.00f * P.instance_density);
    add_pts(allow_mix, P.r_mixed,   2, 0.95f * P.instance_density);
    add_pts(allow_con, P.r_conifer, 3, 0.90f * P.instance_density);
    add_pts(allow_scr, P.r_scrub,   4, 0.60f * P.instance_density);

    if ((int)R.trees.size() > P.max_instances) R.trees.resize((size_t)P.max_instances);

    return R;
}

/*
----------------------------------- Usage -----------------------------------

#include "worldgen/ForestGenerator.hpp"

void build_forests(const std::vector<float>& height01, int W, int H,
                   const std::vector<uint8_t>* waterMask /*optional*/)
{
    worldgen::ForestParams P;
    P.width=W; P.height=H;
    P.riparian_radius_cells = 20.0f;  // stronger riverside bands
    P.north_hemisphere = true;        // set false for SH worlds
    P.instance_density = 1.0f;

    worldgen::ForestResult F = worldgen::GenerateForest(height01, W, H, P, waterMask);

    // Hooks:
    //  • Paint F.canopy01 to drive ground cover density, shade, and wildlife spawns.
    //  • Use F.forest_type to pick species tables and logging yields.
    //  • Drop tree meshes at F.trees (x,y,species_id) with LODs; bake to instancing buffers.
    //  • Block/slow pathfinding in FT_RIPARIAN thickets; favor trails along canopy gaps.
}
*/
} // namespace worldgen
