#pragma once
// ============================================================================
// FertilityGenerator.hpp — farmland suitability & field site generator
// For Colony-Game | C++17 / STL-only
//
// Inputs (grid, W×H):
//   • height01           : normalized height [0..1]
//   • Optional hydrology : flow_accum (uint32), river_mask (uint8), lake_mask (uint8)
//   • Optional climate   : mean_rain_mm (float), gdd_base10 (float)
//
// Outputs:
//   • soil_moisture01    : 0..1 proxy from TWI (ln((k*A)/tanβ)), normalized
//   • fertility01        : 0..1 combined index (terrain + alluvium + climate)
//   • arable_mask        : 1 where fertility >= threshold
//   • field_sites        : Poisson-disk centers (x,y) for farm plots
//
// Notes:
//   • TWI follows the classic Beven–Kirkby form using flow_accum as upslope area
//     proxy; we normalize to 0..1 for gameplay. (See cites in docs.)
//   • If hydrology/climate inputs are omitted, the generator still works using
//     terrain-only signals (slope/flatness + low-elevation bias).
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

namespace worldgen {

struct FertilityParams {
    int   width = 0, height = 0;

    // Terrain interpretation
    float sea_level = 0.50f;                   // height01 <= sea → water
    float meters_per_height_unit = 1200.0f;    // for slope scaling (debug/relative)
    float twi_area_scale = 1.0f;               // k in ln((k*A)/tanβ). Tune per map size.
    float twi_slope_eps  = 1e-4f;              // avoid ln( … / 0 )

    // Fertility blend weights (sum need not be 1)
    float w_moisture   = 0.45f;                // TWI-derived wetness (0..1)
    float w_flatness   = 0.18f;                // 1 - slope01
    float w_alluvium   = 0.22f;                // weighted proximity to rivers (and size)
    float w_lakeshore  = 0.05f;                // gentle boost near lakes
    float w_climate    = 0.10f;                // optional (rain+GDD) if provided

    // Climate optima (used if climate arrays are passed)
    float rain_opt_mm  = 800.0f;               // “nice” annual rainfall baseline
    float rain_sigma   = 500.0f;               // tolerance (mm)
    float gdd_base10_opt = 1800.0f;            // growing degree days (base 10 °C)
    float gdd_sigma      = 800.0f;

    // River/lake influence distances (cells)
    float river_influence_cells = 18.0f;       // ~ floodplain/levee band width
    float lake_influence_cells  = 12.0f;       // shore fringe

    // Arable selection
    float arable_threshold = 0.58f;            // fertility >= → arable
    float field_spacing_min = 12.0f;           // Poisson min spacing (cells)
    int   max_field_sites   = 6000;            // safety cap

    uint64_t seed = 0xA17EF00Du;
};

struct FieldSite { int x=0, y=0; };

struct FertilityResult {
    int width=0, height=0;
    std::vector<float>   slope01;        // 0..1 normalized slope (debug/useful)
    std::vector<float>   soil_moisture01;// 0..1 (normalized TWI)
    std::vector<float>   fertility01;    // 0..1
    std::vector<uint8_t> arable_mask;    // 1 arable, 0 otherwise
    std::vector<FieldSite> field_sites;  // Poisson-disk centers
};

// ------------------------------ internals ------------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

inline std::vector<float> slope01(const std::vector<float>& h,int W,int H,float metersPer){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y))*metersPer;
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1))*metersPer;
        float g=std::sqrt(gx*gx+gy*gy);
        s[I(x,y,W)] = g; gmax = std::max(gmax, g);
    }
    for(float& v : s) v/=gmax;
    return s;
}

// 8-neighborhood Dijkstra distance (float) to a binary mask (1=candidate)
inline std::vector<float> dist_to_mask(const std::vector<uint8_t>& src, int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;
    const int dx[8]={0,1,1,1,0,-1,-1,-1};
    const int dy[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1.f,1.41421356f,1.f,1.41421356f,1.f,1.41421356f,1.f,1.41421356f};
    for(size_t i=0;i<N;++i) if (src[i]){ d[i]=0.f; pq.emplace(0.f,(int)i); }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop(); if (cd>d[i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            int j=(int)I(nx,ny,W); float nd=cd+step[k];
            if (nd<d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

struct RNG { std::mt19937_64 g; explicit RNG(uint64_t s):g(s){} };

inline std::vector<FieldSite> poisson_over_mask(const std::vector<uint8_t>& mask, int W,int H,
                                                float r_cells, int cap, RNG& rng)
{
    const int R=(int)std::ceil(r_cells);
    std::vector<FieldSite> pts; pts.reserve(std::min(cap, W*H/((R*R)+1)));
    std::vector<uint8_t> occ((size_t)W*H,0);

    // randomized scan for blue‑noise-ish distribution
    std::vector<int> order((size_t)W*H); for(size_t i=0;i<order.size();++i) order[i]=(int)i;
    std::shuffle(order.begin(), order.end(), rng.g);

    for(int v : order){
        if (cap>0 && (int)pts.size()>=cap) break;
        if (!mask[(size_t)v]) continue;
        int x=v%W, y=v/W; bool ok=true;
        for(int oy=-R; oy<=R && ok; ++oy){
            for(int ox=-R; ox<=R; ++ox){
                int nx=x+ox, ny=y+oy; if(!inb(nx,ny,W,H)) continue;
                if (ox*ox + oy*oy <= R*R && occ[I(nx,ny,W)]){ ok=false; break; }
            }
        }
        if(!ok) continue;
        occ[(size_t)v]=1; pts.push_back(FieldSite{x,y});
    }
    return pts;
}

// Gaussian-like proximity (0..1) at distance d with "radius" R
inline float near_gauss(float d,float R){ float s=std::max(1e-3f,R*0.75f); return std::exp(-(d*d)/(2.f*s*s)); }

// bell-shaped suitability around an optimum
inline float near_opt(float v,float opt,float sigma){ return std::exp(-0.5f * ( (v-opt)*(v-opt) ) / (sigma*sigma + 1e-6f)); }

} // namespace detail

// -------------------------------- API --------------------------------

inline FertilityResult GenerateFertility(
    const std::vector<float>& height01, int W,int H,
    const FertilityParams& P = {},
    // Optional hydrology
    const std::vector<uint32_t>* flow_accum  /*W*H*/ = nullptr,
    const std::vector<uint8_t>*  river_mask  /*W*H*/ = nullptr,
    const std::vector<uint8_t>*  lake_mask   /*W*H*/ = nullptr,
    // Optional climate
    const std::vector<float>*    mean_rain_mm/*W*H*/ = nullptr,
    const std::vector<float>*    gdd_base10  /*W*H*/ = nullptr)
{
    FertilityResult R; R.width=W; R.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return R;

    // 1) Terrain primitives
    R.slope01 = detail::slope01(height01, W,H, P.meters_per_height_unit);

    // 2) Soil moisture via TWI (ln((k*A)/tanβ)) using flow_accum as A proxy
    R.soil_moisture01.assign(N, 0.0f);
    if (flow_accum){
        std::vector<float> twi(N, 0.0f);
        float mn = 1e30f, mx = -1e30f;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            size_t i=detail::I(x,y,W);
            float A  = std::max(1.0f, (float)(*flow_accum)[i]);    // upstream cells proxy
            float tb = std::max(P.twi_slope_eps, R.slope01[i]);    // ~tanβ in relative units
            float val= std::log( (P.twi_area_scale * A) / tb );
            twi[i]=val; mn=std::min(mn,val); mx=std::max(mx,val);
        }
        float rng = std::max(1e-6f, mx-mn);
        for(size_t i=0;i<N;++i) R.soil_moisture01[i] = detail::clamp01( (twi[i]-mn)/rng );
    }else{
        // Without flow accumulation, use a simple low-slope + low-elevation heuristic
        for(size_t i=0;i<N;++i){
            float low = 1.0f - std::max(0.f, height01[i]-P.sea_level) / std::max(1e-6f, 1.f-P.sea_level);
            R.soil_moisture01[i] = detail::clamp01( 0.7f*(1.f - R.slope01[i]) + 0.3f*low );
        }
    }

    // 3) Alluvial proximity to rivers/lakes
    std::vector<float> d2river, d2lake;
    if (river_mask) d2river = detail::dist_to_mask(*river_mask, W,H);
    if (lake_mask)  d2lake  = detail::dist_to_mask(*lake_mask,  W,H);

    // Build a simple river "size" field on river cells from flow_accum (normalized sqrt)
    std::vector<float> river_size(N, 0.f);
    if (river_mask && flow_accum){
        uint32_t amin=UINT32_MAX, amax=0;
        for(size_t i=0;i<N;++i) if ((*river_mask)[i]){ amin=std::min(amin, (*flow_accum)[i]); amax=std::max(amax, (*flow_accum)[i]); }
        float rng = (amax>amin)? float(amax-amin) : 1.f;
        for(size_t i=0;i<N;++i){
            if (!(*river_mask)[i]) continue;
            float a = ((*flow_accum)[i]-amin)/rng;
            river_size[i] = std::sqrt(std::max(0.f,a));
        }
    }

    // 4) Fertility blend
    R.fertility01.assign(N, 0.0f);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);

        float moist = R.soil_moisture01[i];
        float flat  = 1.0f - R.slope01[i];

        float alluv = 0.0f;
        if (!d2river.empty()){
            float prox = detail::near_gauss(d2river[i], P.river_influence_cells);
            float size = river_size[i]; // nonzero mainly on-channel; prox handles off-channel
            alluv = prox * (0.5f + 0.5f*size);
        }

        float shore = 0.0f;
        if (!d2lake.empty()) shore = detail::near_gauss(d2lake[i], P.lake_influence_cells);

        float clim = 0.5f; // neutral if missing
        if (mean_rain_mm && gdd_base10){
            float r = detail::near_opt((*mean_rain_mm)[i], P.rain_opt_mm, P.rain_sigma);
            float g = detail::near_opt((*gdd_base10)[i],  P.gdd_base10_opt, P.gdd_sigma);
            clim = 0.5f*(r+g);
        }

        float fert = P.w_moisture*moist + P.w_flatness*flat + P.w_alluvium*alluv
                   + P.w_lakeshore*shore + P.w_climate*clim;

        R.fertility01[i] = detail::clamp01(fert);
    }

    // 5) Arable mask
    R.arable_mask.assign(N, 0u);
    for(size_t i=0;i<N;++i) if (R.fertility01[i] >= P.arable_threshold) R.arable_mask[i]=1u;

    // 6) Field sites (blue-noise-ish Poisson sampling over arable)
    detail::RNG rng(P.seed);
    R.field_sites = detail::poisson_over_mask(R.arable_mask, W,H, P.field_spacing_min,
                                              P.max_field_sites>0? P.max_field_sites : 0, rng);
    return R;
}

/*
----------------------------------- Usage -----------------------------------

#include "worldgen/FertilityGenerator.hpp"
// Optional (if you have these):
//   #include "worldgen/HydrologyGenerator.hpp"
//   #include "worldgen/ClimateGenerator.hpp"

void build_farms(const std::vector<float>& height01, int W,int H,
                 // Optional hydro:
                 const std::vector<uint32_t>* flow_accum,
                 const std::vector<uint8_t>*  river_mask,
                 const std::vector<uint8_t>*  lake_mask,
                 // Optional climate:
                 const std::vector<float>*    mean_rain_mm,
                 const std::vector<float>*    gdd_base10)
{
    worldgen::FertilityParams FP;
    FP.width=W; FP.height=H;
    FP.arable_threshold = 0.60f;
    FP.field_spacing_min = 14.0f;   // bigger plots
    FP.river_influence_cells = 20.0f;

    worldgen::FertilityResult FR = worldgen::GenerateFertility(
        height01, W,H, FP, flow_accum, river_mask, lake_mask, mean_rain_mm, gdd_base10);

    // Hooks:
    //  • Paint FR.fertility01 for a "green fertility" overlay.
    //  • Place farms/buildings at FR.field_sites; expand plots outward until
    //    fertility drops below threshold or a road/water boundary is hit.
    //  • Use FR.arable_mask to bias AI job assignment, yields, and crop choices.
}
*/
} // namespace worldgen
