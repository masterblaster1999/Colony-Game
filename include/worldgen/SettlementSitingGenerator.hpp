#pragma once
// ============================================================================
// SettlementSitingGenerator.hpp
// Colony / town / hamlet siting (suitability + blue-noise site picks) and
// simple circular "footprints" for initial zoning.
//
// Inputs (all arrays W*H unless noted):
//   • height01              : normalized height [0..1]
//   • water_mask   (opt)    : 1 = water (ocean/lake/river)
//   • flow_accum   (opt)    : upstream-accumulated flow proxy (uint32/float)
//   • fertility01  (opt)    : farmland suitability [0..1]
//   • road_mask    (opt)    : 1 = existing road (favor access)
//   • hand_m       (opt)    : HAND (meters above nearest drainage)
//
// Outputs:
//   • suitability01         : 0..1 per cell
//   • centers               : chosen settlement centers with score & radius
//   • settlement_id         : int32 (−1 = none, else site index) "footprint"
//   • debug fields          : slope01, d2water (cells)
//
// Notes:
//   • Suitability is a weighted overlay of factors (water, flatness, fertility,
//     access) with an explicit HAND flood penalty. Weighted overlay/site
//     suitability is standard practice in GIS. HAND is routinely used for
//     floodplain mapping. (See README cites.)
//   • Sites are spaced with a Poisson-disk-like greedy sampler (blue-noise),
//     inspired by Bridson (SIGGRAPH 2007). (Implementation here is simple,
//     grid-based; not a verbatim copy.)
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>
#include <random>

namespace worldgen {

struct I2 { int x=0, y=0; };

struct SettlementParams {
    int   width = 0, height = 0;

    // World scale / conversions
    float sea_level = 0.50f;       // height01 <= sea → water (if water_mask missing)
    float cell_size_m = 10.0f;     // meters per grid cell (for distance prefs)
    float hand_flood_full_m = 3.0f;// HAND ≤ this → full flood penalty

    // Weighted overlay (sum of positive factors)
    float w_water_prox   = 0.35f;  // prefer near water (but not too close)
    float w_flatness     = 0.25f;  // prefer low slope
    float w_fertility    = 0.25f;  // prefer arable land (if provided)
    float w_road_access  = 0.10f;  // prefer near roads (if provided)
    float w_confluence   = 0.05f;  // prefer near strong rivers/confluences (if flow provided)

    // Penalties
    float flood_penalty  = 0.65f;  // subtract flood danger (from HAND)
    float water_too_close_penalty = 0.30f; // subtract if inside "unsafe" ring

    // Water distance preference: ring target (safe walking distance above bank)
    float ideal_water_dist_m = 120.0f; // center of Gaussian preference
    float ideal_water_sigma_m= 80.0f;  // width of preference
    float unsafe_water_buffer_m = 40.0f; // inside this → apply "too close" penalty

    // Slope penalty (slope01 is 0..1 normalized gradient)
    float slope_penalty_start01 = 0.30f;  // begin penalizing above this
    float slope_penalty_full01  = 0.70f;  // full penalty by here

    // Site picking (blue-noise-ish)
    int   max_sites = 8;             // cap
    float min_site_spacing_cells = 60.0f; // Poisson min spacing
    float min_score_to_seed = 0.55f; // absolute floor to consider as a site
    float footprint_radius_cells = 20.0f; // base circular footprint radius

    // RNG
    uint64_t seed = 0x51TE517Eu;
};

struct SettlementCenter {
    int   x=0, y=0;
    float score=0.f;        // suitability at pick time
    float radius_cells=0.f; // chosen footprint radius
};

struct SettlementResult {
    int width=0, height=0;
    std::vector<float>   suitability01; // W*H
    std::vector<uint8_t> slope01;       // 0..255 for debug/visualization
    std::vector<int>     d2water;       // int cells (debug)
    std::vector<int32_t> settlement_id; // -1 none, else index
    std::vector<SettlementCenter> centers;
};

// --------------------------- internals ---------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

inline std::vector<float> slope01_from_height(const std::vector<float>& h,int W,int H){
    std::vector<float> s((size_t)W*H,0.f);
    auto Hs=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return h[I(x,y,W)]; };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[I(x,y,W)]=g; gmax=std::max(gmax,g);
    }
    for(float& v : s) v = (gmax>0.f)? v/gmax : 0.f;
    return s;
}

// Integer 8-neigh BFS distance (cells) to a binary mask (1=candidate)
inline std::vector<int> dist8_to_mask(const std::vector<uint8_t>& mask,int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<int> d(N, INT_MAX);
    std::queue<int> q;
    for(size_t i=0;i<N;++i){ if (mask[i]){ d[i]=0; q.push((int)i);} }
    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    while(!q.empty()){
        int v=q.front(); q.pop();
        int x=v%W, y=v/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            size_t j=I(nx,ny,W);
            if (d[j] > d[(size_t)v]+1){ d[j]=d[(size_t)v]+1; q.push((int)j); }
        }
    }
    return d;
}

inline float gauss_pref(float d_m, float ideal_m, float sigma_m){
    float z=(d_m-ideal_m)/std::max(1e-3f,sigma_m);
    return std::exp(-0.5f*z*z);
}
inline float slope_penalty(float s01,float start,float full){
    if (full<=start) return (s01<=start)? 0.f : 1.f;
    if (s01<=start) return 0.f;
    if (s01>=full)  return 1.f;
    float t=(s01-start)/(full-start);
    return detail::clamp01(t);
}

inline std::vector<uint8_t> derive_water(const std::vector<float>& h,int W,int H,float sea){
    std::vector<uint8_t> m((size_t)W*H,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x)
        m[I(x,y,W)] = (h[I(x,y,W)] <= sea) ? 1u : 0u;
    return m;
}

// Simple "confluence strength": highlight places where flow increases sharply
// along either axis/diagonals (proxy for tributaries merging).
inline std::vector<float> confluence_strength(const std::vector<float>& flow01,int W,int H){
    if (flow01.empty()) return {};
    std::vector<float> c((size_t)W*H,0.f);
    auto F=[&](int x,int y){ x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1); return flow01[I(x,y,W)]; };
    float mx=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float here = F(x,y);
        float inc1 = std::max(0.f, F(x+1,y)-here) + std::max(0.f, F(x-1,y)-here);
        float inc2 = std::max(0.f, F(x,y+1)-here) + std::max(0.f, F(x,y-1)-here);
        float incd = std::max(0.f, F(x+1,y+1)-here) + std::max(0.f, F(x-1,y-1)-here)
                   + std::max(0.f, F(x+1,y-1)-here) + std::max(0.f, F(x-1,y+1)-here);
        float v = 0.5f*(inc1+inc2) + 0.25f*incd;
        c[I(x,y,W)] = v; mx=std::max(mx,v);
    }
    if (mx>0.f) for(float& v:c) v/=mx;
    return c;
}

struct RNG { std::mt19937_64 g; explicit RNG(uint64_t s):g(s){} float uf(){ return std::uniform_real_distribution<float>(0.f,1.f)(g);} };

} // namespace detail

// ---------------------------------- API ----------------------------------

inline SettlementResult GenerateSettlementSites(
    const std::vector<float>& height01, int W,int H,
    const SettlementParams& P = {},
    // Optional layers (W*H)
    const std::vector<uint8_t>* water_mask   = nullptr,
    const std::vector<float>*   flow_accum   = nullptr, // upstream cells (or any positive proxy)
    const std::vector<float>*   fertility01  = nullptr,
    const std::vector<uint8_t>* road_mask    = nullptr,
    const std::vector<float>*   hand_m       = nullptr)
{
    SettlementResult R; R.width=W; R.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return R;

    // 1) Base fields
    auto slope01 = detail::slope01_from_height(height01, W,H);
    R.suitability01.assign(N, 0.f);
    R.slope01.resize(N);
    for(size_t i=0;i<N;++i) R.slope01[i] = (uint8_t)std::lround(detail::clamp01(slope01[i])*255.f);

    // Water mask (derive from sea level if not provided)
    std::vector<uint8_t> wmask = water_mask ? *water_mask : detail::derive_water(height01, W,H, P.sea_level);
    R.d2water = detail::dist8_to_mask(wmask, W,H);

    // Flow normalized 0..1
    std::vector<float> flow01;
    if (flow_accum && !flow_accum->empty()){
        float mn=std::numeric_limits<float>::infinity(), mx=-mn;
        for(float v : *flow_accum){ mn=std::min(mn,v); mx=std::max(mx,v); }
        float rg = std::max(1e-6f, mx-mn);
        flow01.resize(N);
        for(size_t i=0;i<N;++i) flow01[i] = std::sqrt(std::max(0.f, ((*flow_accum)[i]-mn)/rg));
    }
    auto confl = detail::confluence_strength(flow01, W,H);

    // Road distance (optional)
    std::vector<int> d2road;
    if (road_mask && !road_mask->empty()){
        d2road = detail::dist8_to_mask(*road_mask, W,H);
    }

    // 2) Score each cell (weighted overlay with penalties)
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);
        if (wmask[i]){ R.suitability01[i]=0.f; continue; } // no water cells

        // proximity to water, but not too close
        float dist_m = (float)R.d2water[i] * P.cell_size_m;
        float water_pref = detail::gauss_pref(dist_m, P.ideal_water_dist_m, P.ideal_water_sigma_m);
        float too_close = (dist_m < P.unsafe_water_buffer_m) ? 1.f : 0.f;

        // flatness (inverse of slope)
        float flat = 1.f - slope01[i];
        // slope "unsuitability" ramps up beyond start/full thresholds
        float slope_pen = detail::slope_penalty(slope01[i], P.slope_penalty_start01, P.slope_penalty_full01);

        // soil/farming
        float fert = fertility01 ? detail::clamp01((*fertility01)[i]) : 0.5f; // neutral if missing

        // access to roads
        float access = 0.f;
        if (!d2road.empty()){
            float dm = (float)d2road[i] * P.cell_size_m;
            // gentle 1/(1+dm) shaped into [0..1] with a Gaussian-ish preference for near but not on top
            access = std::exp(-(dm*dm)/(2.f*400.f)); // ~ <=20m sweet spot if cell=10m
        }

        // confluence / major river preference
        float conf = confl.empty()? 0.f : confl[i];

        // flood penalty via HAND
        float flood = 0.f;
        if (hand_m && !hand_m->empty()){
            float h = std::max(0.f, (*hand_m)[i]); // meters above nearest drainage
            flood = detail::clamp01(1.f - (h / std::max(1e-3f, P.hand_flood_full_m)));
        }

        float score = 0.f;
        score += P.w_water_prox  * water_pref;
        score += P.w_flatness    * flat * (1.f - slope_pen);
        score += P.w_fertility   * fert;
        score += P.w_road_access * access;
        score += P.w_confluence  * conf;

        score -= P.flood_penalty * flood;
        score -= P.water_too_close_penalty * too_close;

        R.suitability01[i] = detail::clamp01(score);
    }

    // 3) Pick well-spaced centers (greedy blue-noise over descending score)
    std::vector<int> order(N); for(size_t i=0;i<N;++i) order[i]=(int)i;
    std::sort(order.begin(), order.end(), [&](int a,int b){
        return R.suitability01[(size_t)a] > R.suitability01[(size_t)b];
    });

    std::vector<uint8_t> taken(N,0);
    auto far_enough=[&](int x,int y){
        int Rcells=(int)std::ceil(P.min_site_spacing_cells);
        for(int oy=-Rcells;oy<=Rcells;++oy){
            for(int ox=-Rcells;ox<=Rcells;++ox){
                int nx=x+ox, ny=y+oy; if(!detail::inb(nx,ny,W,H)) continue;
                if (taken[detail::I(nx,ny,W)]) return false;
            }
        }
        return true;
    };

    for(int idx : order){
        if ((int)R.centers.size() >= P.max_sites) break;
        if (R.suitability01[(size_t)idx] < P.min_score_to_seed) break;

        int x=idx%W, y=idx/W;
        if (wmask[(size_t)idx]) continue;
        if (!far_enough(x,y)) continue;

        // Mark a blocked disk to maintain spacing
        int Rcells=(int)std::ceil(P.min_site_spacing_cells);
        for(int oy=-Rcells;oy<=Rcells;++oy){
            for(int ox=-Rcells;ox<=Rcells;++ox){
                int nx=x+ox, ny=y+oy; if(!detail::inb(nx,ny,W,H)) continue;
                if ((ox*ox + oy*oy) <= Rcells*Rcells) taken[detail::I(nx,ny,W)] = 1;
            }
        }

        // radius scales with local fertility/flatness (simple heuristic)
        float fert = fertility01 ? detail::clamp01((*fertility01)[(size_t)idx]) : 0.5f;
        float flat = 1.f - slope01[(size_t)idx];
        float rad  = P.footprint_radius_cells * (0.8f + 0.4f * 0.5f*(fert+flat));

        R.centers.push_back(SettlementCenter{ x,y, R.suitability01[(size_t)idx], rad });
    }

    // 4) Stamp circular footprints into settlement_id
    R.settlement_id.assign(N, -1);
    for (int sid=0; sid<(int)R.centers.size(); ++sid){
        const auto& c = R.centers[(size_t)sid];
        int Rcells = std::max(2, (int)std::lround(c.radius_cells));
        for(int oy=-Rcells; oy<=Rcells; ++oy){
            for(int ox=-Rcells; ox<=Rcells; ++ox){
                int nx=c.x+ox, ny=c.y+oy; if(!detail::inb(nx,ny,W,H)) continue;
                if ((ox*ox + oy*oy) <= Rcells*Rcells && !wmask[detail::I(nx,ny,W)]){
                    R.settlement_id[detail::I(nx,ny,W)] = sid;
                }
            }
        }
    }

    return R;
}

/*
----------------------------------- Usage -----------------------------------

#include "worldgen/SettlementSitingGenerator.hpp"

// Example wiring (supplying optional layers if you have them):
void place_initial_settlements(
    const std::vector<float>& height01, int W,int H,
    const std::vector<uint8_t>* waterMask,       // 1=water
    const std::vector<float>*   flowAccum,       // upstream area proxy
    const std::vector<float>*   fertility01,     // 0..1
    const std::vector<uint8_t>* roadMask,        // 1=road
    const std::vector<float>*   hand_m)          // meters above nearest drainage
{
    worldgen::SettlementParams P;
    P.width=W; P.height=H;
    P.cell_size_m = 10.0f;     // set to your grid scale
    P.max_sites   = 6;
    P.min_site_spacing_cells = 70.0f;
    P.hand_flood_full_m = 3.0f;
    P.ideal_water_dist_m = 120.0f;

    worldgen::SettlementResult S = worldgen::GenerateSettlementSites(
        height01, W,H, P, waterMask, flowAccum, fertility01, roadMask, hand_m);

    // Hooks:
    //   • Use S.centers to seed town/hamlet generation and connect with your RoadNetworkGenerator.
    //   • Use S.settlement_id as a "zoning mask" for house/workshop placement.
    //   • Visualize S.suitability01 to tweak weights quickly.
}
*/
} // namespace worldgen
