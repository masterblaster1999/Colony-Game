#pragma once
// ============================================================================
// ForestHydroBridge.hpp — Merge forests with hydrology (header-only, C++17)
//   • Builds a "riparian_boost" field using streams (flow accumulation), lakes,
//     and HAND floodplain, then updates forest moisture/canopy/classification.
//
// Works with any hydrology source as long as you supply:
//   - stream_mask (W*H, 1 = stream)
//   - flow_accum  (W*H, >=1; upslope cell count or proxy for discharge)
//   - lake_mask   (W*H, optional; 1 = lake/standing water)
//   - hand_m      (W*H, optional; meters above nearest drainage)
//
// References (concepts; implementation here is original):
//   - Riparian zones & buffer widths (EPA/USGS).  [see README cite]
//   - Hydraulic geometry (width ~ discharge^b).    [Leopold & Maddock]
//   - HAND floodplain mapping.                     [NOAA/NWM]
//   - Flow accumulation (D8/MFD/D∞) in GIS.
//
// If you used the ForestGenerator.hpp earlier, its ForestType values were:
//   0 NONE, 1 RIPARIAN, 2 DECIDUOUS, 3 MIXED, 4 CONIFER, 5 SCRUB.
// If not, pass your own "riparianTypeValue" below.
// ============================================================================

#include <vector>
#include <queue>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace worldgen {

struct FH_Params {
    int   width = 0, height = 0;

    // --- Stream influence ---
    float base_buffer_cells      = 6.0f;   // ≈ 6 cells ~ 120m if cell=20m (tune to your scale)
    float strength_from_logA     = 1.0f;   // log(1+flow_accum) weight at stream centers
    float strength_from_order    = 0.35f;  // extra per Strahler order step (if provided)
    float half_decay_cells       = 8.0f;   // distance where stream influence halves

    // --- Lake influence ---
    float lake_buffer_cells      = 10.0f;  // fringe width around lake edges

    // --- Floodplain (HAND) influence ---
    float hand_full_wet_m        = 3.0f;   // HAND <= this → strongly riparian/wetland

    // --- Blend weights (sum doesn’t need to be 1) ---
    float w_stream               = 0.6f;
    float w_lake                 = 0.15f;
    float w_flood                = 0.25f;

    // --- Forest updates ---
    float moisture_gain          = 0.50f;  // added to moisture01 scaled by riparian_boost
    float canopy_gain            = 0.35f;  // added to canopy01 scaled by riparian_boost
    float classify_threshold     = 0.55f;  // riparian_boost >= this → set forest_type to RIPARIAN

    // Forest type code to write when classifying riparian (matches our earlier header)
    uint8_t riparianTypeValue    = 1;      // 1 = FT_RIPARIAN in the provided ForestGenerator.hpp

    // Safety / behavior
    bool   keep_out_of_water     = true;   // don’t classify water/lakes as riparian
};

// Convenience wrapper to pass hydrology arrays
struct FH_Hydro {
    // Required
    const std::vector<uint8_t>* stream_mask = nullptr; // W*H, 1=stream
    const std::vector<float>*   flow_accum  = nullptr; // W*H (>=1)
    // Optional
    const std::vector<uint8_t>* strahler    = nullptr; // W*H (0 if n/a)
    const std::vector<uint8_t>* lake_mask   = nullptr; // W*H, 1=lake
    const std::vector<float>*   hand_m      = nullptr; // W*H, meters
    const std::vector<uint8_t>* water_mask  = nullptr; // W*H, 1=water (ocean)
};

// Forest fields to update in place
struct FH_Forest {
    std::vector<float>*   moisture01 = nullptr; // W*H
    std::vector<float>*   canopy01   = nullptr; // W*H
    std::vector<uint8_t>* forest_type= nullptr; // W*H
};

// -------------------------- Internals --------------------------

namespace detail {
inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

// Multi-source 8-neigh integer distance to mask (1=candidate)
inline std::vector<int> dist8_to_mask(const std::vector<uint8_t>& mask, int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<int> d(N, INT_MAX);
    std::queue<int> q;
    for(size_t i=0;i<N;++i) if (mask[i]){ d[i]=0; q.push((int)i); }

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

// Propagate a decaying stream "strength" outward (max‑pooling with decay).
// seed[i] > 0 at streams, else 0; decay_per_step in (0,1).
inline std::vector<float> propagate_strength(const std::vector<float>& seed, int W,int H, float decay_per_step){
    const size_t N=(size_t)W*H;
    std::vector<float> f = seed; // start with seed strengths
    std::queue<int> q;
    for(size_t i=0;i<N;++i) if (f[i] > 0.f) q.push((int)i);

    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};

    while(!q.empty()){
        int v=q.front(); q.pop();
        int x=v%W, y=v/W;
        float val = f[(size_t)v];
        float next = val * decay_per_step;
        if (next < 1e-4f) continue; // early out for tiny influence
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k]; if(!inb(nx,ny,W,H)) continue;
            size_t j=I(nx,ny,W);
            if (next > f[j] + 1e-6f){ f[j]=next; q.push((int)j); }
        }
    }
    return f;
}

} // namespace detail

// --------------------------- API ---------------------------

// 1) Build a 0..1 "riparian_boost" field from hydrology.
inline std::vector<float> BuildRiparianBoost(const FH_Params& P, const FH_Hydro& H){
    const int W=P.width, Ht=P.height; const size_t N=(size_t)W*Ht;
    std::vector<float> boost(N, 0.f);
    if (!H.stream_mask || !H.flow_accum || H.stream_mask->size()!=N || H.flow_accum->size()!=N) return boost;

    // --- STREAMS: seed with log(1+A) (+ order bonus), then decay outward ---
    std::vector<float> seed(N, 0.f);
    float maxSeed=1e-6f;
    for(size_t i=0;i<N;++i){
        if ((*H.stream_mask)[i]){
            float A   = std::max(1.0f, (*H.flow_accum)[i]);
            float s   = P.strength_from_logA * std::log1p(A);
            if (H.strahler && !H.strahler->empty())
                s += P.strength_from_order * std::max(0, (int)(*H.strahler)[i] - 1);
            seed[i] = std::max(0.f, s);
            if (seed[i] > maxSeed) maxSeed = seed[i];
        }
    }
    if (maxSeed <= 0.f) return boost;

    // Decay so that influence halves every "half_decay_cells"
    float perStep = std::pow(0.5f, 1.0f / std::max(1e-3f, P.half_decay_cells));
    auto streamField = detail::propagate_strength(seed, W, Ht, perStep);
    float maxStream = *std::max_element(streamField.begin(), streamField.end());
    if (maxStream > 0.f) for (float& v : streamField) v /= maxStream;

    // --- LAKES: fringe by distance ---
    std::vector<float> lakeField(N, 0.f);
    if (H.lake_mask && !H.lake_mask->empty()){
        auto d2lake = detail::dist8_to_mask(*H.lake_mask, W,Ht);
        for(size_t i=0;i<N;++i){
            if (d2lake[i]==INT_MAX) continue;
            float t = (float)d2lake[i] / std::max(1e-3f, P.lake_buffer_cells);
            lakeField[i] = std::exp(-(t*t)); // Gaussian falloff
        }
    }

    // --- FLOODPLAIN (HAND): 0 at dry, 1 near streams at low HAND ---
    std::vector<float> floodField(N, 0.f);
    if (H.hand_m && !H.hand_m->empty()){
        for(size_t i=0;i<N;++i){
            float hand = std::max(0.f, (*H.hand_m)[i]);
            floodField[i] = detail::clamp01(1.0f - (hand / std::max(1e-3f, P.hand_full_wet_m)));
        }
    }

    // Blend
    for(size_t i=0;i<N;++i){
        boost[i] = detail::clamp01(P.w_stream*streamField[i] + P.w_lake*lakeField[i] + P.w_flood*floodField[i]);
    }
    return boost;
}

// 2) Apply the boost to forest fields (moisture/canopy) and classify riparian cells.
//    Pass water/lake masks to keep water from being flagged as forest.
inline void ApplyRiparianToForest(const FH_Params& P, const FH_Hydro& H,
                                  const std::vector<float>& riparian_boost,
                                  FH_Forest F)
{
    const int W=P.width, Ht=P.height; const size_t N=(size_t)W*Ht;
    if (!F.moisture01 || !F.canopy01 || !F.forest_type) return;
    if (F.moisture01->size()!=N || F.canopy01->size()!=N || F.forest_type->size()!=N) return;

    for(size_t i=0;i<N;++i){
        // In‑place moisture/canopy adjustments
        (*F.moisture01)[i] = detail::clamp01((*F.moisture01)[i] + P.moisture_gain * riparian_boost[i]);
        (*F.canopy01)[i]   = detail::clamp01((*F.canopy01)[i]   + P.canopy_gain   * riparian_boost[i]);

        // Optional classification to RIPARIAN
        bool water = (H.water_mask && !H.water_mask->empty() && (*H.water_mask)[i]) ||
                     (H.lake_mask  && !H.lake_mask->empty()  && (*H.lake_mask)[i]) ||
                     (H.stream_mask && !H.stream_mask->empty() && (*H.stream_mask)[i]);
        if (riparian_boost[i] >= P.classify_threshold){
            if (!P.keep_out_of_water || !water){
                (*F.forest_type)[i] = P.riparianTypeValue;
            }
        }
    }
}

// 3) Convenience: build a boolean mask of "riparian cells" you can use for spawning.
inline std::vector<uint8_t> BuildRiparianMask(const std::vector<float>& riparian_boost, float threshold, int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<uint8_t> m(N,0);
    for(size_t i=0;i<N;++i) if (riparian_boost[i] >= threshold) m[i]=1;
    return m;
}

} // namespace worldgen
