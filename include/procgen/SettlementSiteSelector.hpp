#pragma once
// ============================================================================
// SettlementSiteSelector.hpp — header-only site suitability & settlement picker
// For Colony-Game (C++17 / STL-only)
//
// What it does
// ------------
// 1) Builds a continuous "suitability" field S(x,y) combining:
//    • proximity to water (useful), with flood-risk penalty,
//    • slope (prefers gentle grades),
//    • elevation above sea level (avoid floodplain/extremes),
//    • optional fertility (boost good farmland).
// 2) Smooths S for coherent patches.
// 3) Selects blue-noise-spaced local maxima for Town / Hamlet / Outpost tiers.
//
// Why this matches real practice
// ------------------------------
// • GIS-style multi-criteria suitability analysis: blend slope, distance-to-water,
//   and elevation into a suitability surface, then pick sites subject to constraints.
//   (See ArcGIS suitability modeling guidance.)  // references in PR/README.
//
// Outputs
// -------
// • Suitability map (float, W*H)
// • Debug fields: slope01, dist_to_water, flood_risk
// • Picked sites (towns/hamlets/outposts) with score breakdown
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>
#include <random>
#include <utility>
#include <numeric>

namespace procgen {

// --------------------------- Parameters & Results ---------------------------

struct SiteParams {
    // World & units
    float sea_level = 0.50f;                 // normalized [0..1]
    float meters_per_height_unit = 1200.0f;  // for slope normalization (relative only)

    // Suitability weights (sum doesn't need to be 1.0)
    float w_water     = 0.45f;   // prefer access to water (useful)
    float w_slope     = 0.30f;   // prefer gentle slopes
    float w_elev      = 0.15f;   // prefer mid-high above sea
    float w_fertility = 0.10f;   // optional external fertility layer

    // Water / flooding model (cells + meters_per_height_unit)
    float ideal_water_dist = 10.0f; // best distance to water (too close ⇒ flood risk)
    float flood_margin     = 6.0f;  // meters above sea treated as flood-prone band

    // Light smoothing for coherence
    int   blur_radius      = 2;     // box blur radius per axis; 0 disables

    // How many to pick per tier
    int   towns    = 3;
    int   hamlets  = 7;
    int   outposts = 10;

    // Minimum spacing (cells)
    float min_sep_town    = 60.0f;
    float min_sep_hamlet  = 40.0f;
    float min_sep_outpost = 28.0f;

    // Candidate pruning
    float   keep_threshold = 0.25f;
    int     max_candidates = 50000;
    uint64_t rng_seed      = 0xBEEFCAFEu;
};

struct Site {
    int x=0, y=0;
    float score=0.0f;      // final suitability
    // Per-term contributions (for debug/UI)
    float water_term=0.0f, slope_term=0.0f, elev_term=0.0f, fert_term=0.0f;
    const char* kind = "outpost"; // "town"/"hamlet"/"outpost"
};

struct SiteResult {
    int width=0, height=0;

    // Size W*H fields
    std::vector<float> suitability;     // 0..1
    std::vector<float> slope01;         // 0..1
    std::vector<float> dist_to_water;   // cells (8-neigh metric)
    std::vector<float> flood_risk01;    // 0..1 (1 = high risk)

    // Picks
    std::vector<Site> towns, hamlets, outposts;

    // Debug: a mask marking chosen cells
    std::vector<uint8_t> picked_mask;   // size W*H
};

// ------------------------------ Utilities ----------------------------------

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

inline std::vector<float> slope01(const std::vector<float>& h, int W, int H, float metersPerUnit){
    std::vector<float> s((size_t)W*H, 0.f);
    auto Hs=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[I(x,y,W)];
    };
    float maxg = 1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy) * metersPerUnit;
        s[I(x,y,W)] = g;
        if (g>maxg) maxg=g;
    }
    for(float& v: s) v/=maxg;
    return s;
}

// Multi-source distance to water (8-neigh Dijkstra with unit/diag weights)
inline std::vector<float> dist_to_mask(const std::vector<uint8_t>& mask, int W, int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    const int dx8[8]={0,1,1,1,0,-1,-1,-1};
    const int dy8[8]={-1,-1,0,1,1,1,0,-1};
    const float step[8]={1.f,1.4142f,1.f,1.4142f,1.f,1.4142f,1.f,1.4142f};
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=I(x,y,W);
        if (mask[i]){ d[i]=0.f; pq.emplace(0.f, (int)i); }
    }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop();
        if (cd>d[i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx8[k], ny=y+dy8[k];
            if(!inb(nx,ny,W,H)) continue;
            int j=(int)I(nx,ny,W);
            float nd=cd+step[k];
            if (nd<d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

inline void box_blur(std::vector<float>& f, int W, int H, int r){
    if (r<=0) return;
    std::vector<float> tmp((size_t)W*H,0.f);
    // horizontal
    for(int y=0;y<H;++y){
        float acc=0.f;
        for(int x=-r;x<=r;++x) acc += f[(size_t)I(std::clamp(x,0,W-1),y,W)];
        for(int x=0;x<W;++x){
            tmp[I(x,y,W)] = acc / (2*r+1);
            int x_add = std::min(W-1, x+r+1);
            int x_rem = std::max(0,     x-r);
            acc += f[I(x_add,y,W)] - f[I(x_rem,y,W)];
        }
    }
    // vertical
    for(int x=0;x<W;++x){
        float acc=0.f;
        for(int y=-r;y<=r;++y) acc += tmp[(size_t)I(x,std::clamp(y,0,H-1),W)];
        for(int y=0;y<H;++y){
            f[I(x,y,W)] = acc / (2*r+1);
            int y_add = std::min(H-1, y+r+1);
            int y_rem = std::max(0,     y-r);
            acc += tmp[I(x,y_add,W)] - tmp[I(x,y_rem,W)];
        }
    }
}

inline bool far_enough(const std::vector<Site>& chosen, int x, int y, float minsep){
    float m2 = minsep*minsep;
    for(const auto& s: chosen){
        float dx=float(x - s.x), dy=float(y - s.y);
        if (dx*dx+dy*dy < m2) return false;
    }
    return true;
}

} // namespace detail

// ------------------------------- Main API -----------------------------------

inline SiteResult GenerateSettlementSites(
    const std::vector<float>& height01, int W, int H,
    const SiteParams& P = {},
    const std::vector<uint8_t>* waterMask /*optional W*H*/ = nullptr,
    const std::vector<float>*   fertility01 /*optional W*H in 0..1*/ = nullptr)
{
    SiteResult out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N){ return out; }

    // 1) Water mask (derive from sea level if not provided)
    std::vector<uint8_t> water(N,0);
    if (waterMask){ water = *waterMask; }
    else { for(size_t i=0;i<N;++i) water[i] = (height01[i] <= P.sea_level) ? 1 : 0; }

    // 2) Terrain primitives
    out.slope01       = detail::slope01(height01, W, H, P.meters_per_height_unit);
    out.dist_to_water = detail::dist_to_mask(water, W, H);

    // Flood risk: high when near water *and* barely above sea
    out.flood_risk01.resize(N,0.f);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);
        float aboveM = std::max(0.f, height01[i] - P.sea_level) * P.meters_per_height_unit;
        float nearW  = std::exp(- (out.dist_to_water[i]*out.dist_to_water[i]) /
                                (P.ideal_water_dist*P.ideal_water_dist + 1e-6f));
        float low    = std::exp(- (aboveM*aboveM) / (P.flood_margin*P.flood_margin + 1e-6f));
        out.flood_risk01[i] = detail::clamp01( nearW * low );
    }

    // 3) Suitability terms (0..1)
    out.suitability.assign(N, 0.f);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);
        if (water[i]){ out.suitability[i]=0.f; continue; } // no sites in water

        // Water term: access good, flood bad
        float nearW = std::exp(- (out.dist_to_water[i]*out.dist_to_water[i]) /
                               (P.ideal_water_dist*P.ideal_water_dist + 1e-6f));
        float water_term = detail::clamp01( nearW * (1.f - out.flood_risk01[i]) );

        // Slope & elevation
        float slope_term = 1.f - out.slope01[i];
        float above = std::max(0.f, height01[i] - P.sea_level) / std::max(1e-6f, (1.f - P.sea_level));
        float elev_term = std::exp( - std::pow((above - 0.35f)/0.25f, 2.f) ); // bell curve centered ~35% up

        // Fertility (optional)
        float fert_term = fertility01 ? detail::clamp01((*fertility01)[i]) : 0.f;

        float score = P.w_water*water_term + P.w_slope*slope_term + P.w_elev*elev_term + P.w_fertility*fert_term;
        out.suitability[i] = detail::clamp01(score);
    }

    // 4) Smooth for coherence
    if (P.blur_radius > 0){ detail::box_blur(out.suitability, W, H, P.blur_radius); }

    // 5) Collect candidate local maxima above threshold
    struct Cand { int x,y; float s, w, sl, el, ft; };
    std::vector<Cand> cands; cands.reserve(std::min<int>(P.max_candidates, W*H));

    for(int y=1;y<H-1;++y) for(int x=1;x<W-1;++x){
        size_t i=detail::I(x,y,W);
        float s = out.suitability[i];
        if (s < P.keep_threshold) continue;

        // non-maximum suppression (4-neigh)
        if (s < out.suitability[detail::I(x+1,y,W)] ||
            s < out.suitability[detail::I(x-1,y,W)] ||
            s < out.suitability[detail::I(x,y+1,W)] ||
            s < out.suitability[detail::I(x,y-1,W)]) continue;

        float nearW = std::exp(- (out.dist_to_water[i]*out.dist_to_water[i]) /
                               (P.ideal_water_dist*P.ideal_water_dist + 1e-6f));
        float water_term = detail::clamp01( nearW * (1.f - out.flood_risk01[i]) );
        float slope_term = 1.f - out.slope01[i];
        float above = std::max(0.f, height01[i] - P.sea_level) / std::max(1e-6f, (1.f - P.sea_level));
        float elev_term = std::exp( - std::pow((above - 0.35f)/0.25f, 2.f) );
        float fert_term = fertility01 ? detail::clamp01((*fertility01)[i]) : 0.f;

        cands.push_back({x,y,s, water_term, slope_term, elev_term, fert_term});
        if ((int)cands.size() >= P.max_candidates) break;
    }

    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b){ return a.s > b.s; });

    // 6) Greedy blue-noise spacing per tier
    auto pick_tier = [&](int want, float minsep, const char* label){
        std::vector<Site> sites; sites.reserve(want);
        for(const auto& c : cands){
            if ((int)sites.size() >= want) break;
            if (!detail::far_enough(sites, c.x, c.y, minsep)) continue;
            sites.push_back( Site{ c.x, c.y, c.s, c.w, c.sl, c.el, c.ft, label } );
        }
        return sites;
    };

    out.towns    = pick_tier(P.towns,    P.min_sep_town,    "town");
    out.hamlets  = pick_tier(P.hamlets,  P.min_sep_hamlet,  "hamlet");
    out.outposts = pick_tier(P.outposts, P.min_sep_outpost, "outpost");

    // Mark picks
    out.picked_mask.assign(N, 0);
    for (auto vec : { &out.towns, &out.hamlets, &out.outposts }){
        for (const auto& s : *vec) out.picked_mask[detail::I(s.x,s.y,W)] = 1;
    }

    return out;
}

/*
------------------------------------ Usage ------------------------------------

#include "procgen/SettlementSiteSelector.hpp"

void choose_settlement_sites(const std::vector<float>& height01, int W, int H,
                             const std::vector<uint8_t>* waterMask /*optional*/,
                             const std::vector<float>* fertility01 /*optional*/)
{
    procgen::SiteParams P;
    P.towns = 3; P.hamlets = 8; P.outposts = 12;
    P.min_sep_town = 64; P.min_sep_hamlet = 44; P.min_sep_outpost = 28;
    P.ideal_water_dist = 10.0f; P.flood_margin = 6.0f;

    procgen::SiteResult R = procgen::GenerateSettlementSites(height01, W, H, P, waterMask, fertility01);

    // Hooks:
    // • Place Town Center / Markets at R.towns; hamlets spawn early jobs; outposts for mines/watchtowers.
    // • Feed R.towns+hamlets to your pathfinder to lay main roads; use slope/water for costs.
    // • Visualize suitability/flood_risk overlays for debugging and AI hints.
}
*/
} // namespace procgen
