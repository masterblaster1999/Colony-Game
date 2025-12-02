#pragma once
// ============================================================================
// VegetationScatter.hpp — header-only, variable-density blue-noise scatterer
// For Colony-Game (C++17 / STL-only)
//
// What it does
// ------------
// • Places vegetation/props (trees, bushes, rocks, animals, etc.) with
//   blue-noise spacing (Bridson 2007) for a natural, unclumped look.
// • Lets density vary with terrain via a per-position radius function:
//   more desirable -> smaller radius -> more points (variable-density PD).
// • Terrain heuristics included: slope, elevation vs sea level, distance to water.
// • Chunk-friendly: accepts boundary seeds to stitch neighboring chunks.
// • Deterministic via 64-bit seed.
//
// References (concepts; implementation is original here):
// • Bridson, “Fast Poisson Disk Sampling in Arbitrary Dimensions” (O(N)).   [SIGGRAPH 2007]
// • Red Blob Games notes on Poisson disc for maps & boundary handling.      [practical]
// • Variable-density Poisson-disc sampling (surveys/examples).              [Dwork+ 2020; Mitchell+]
//
// ============================================================================

#include <vector>
#include <array>
#include <random>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

namespace worldgen {

// --------------------- Public API ---------------------

struct F2 { float x{}, y{}; };

struct SpeciesParams {
    // Display/debug name is optional; not used by the algorithm.
    const char* name = "species";

    // Desired spacing: the sampler will vary the radius in [minRadius, maxRadius]
    // by reading the "desirability" (0..1). Higher desirability => smaller radius.
    float minRadius = 8.0f;   // cells
    float maxRadius = 20.0f;  // cells

    // How much each terrain factor contributes to desirability (0..1)
    // The built-in desirability is: wMoisture*m + wFlat*(1-slope) + wLowland*lowland
    float wMoisture = 0.6f;   // like trees near water
    float wFlat     = 0.3f;   // avoid steep slopes
    float wLowland  = 0.1f;   // mild bias to lower elevations

    // Optional additional scalar multiplier for the final density (0 disables)
    float densityBoost = 1.0f;     // multiply desirability before mapping to radius

    // Clamp the final desirability range to control presence
    float minPresence = 0.0f;      // raise to exclude bad areas entirely
};

struct ScatterParams {
    int      kCandidates = 30;   // Bridson k (15..30 typical)
    uint64_t seed        = 1337; // deterministic seed

    // If you generate per-chunk, pass seeds from neighbor edges to avoid seams.
    // These are pre-existing points that will be enforced but not emit new samples.
    std::vector<F2> boundarySeeds;

    // Terrain interpretation
    float seaLevel              = 0.50f; // in [0..1] height units
    float metersPerHeightUnit   = 1200.0f; // for slope normalization
};

struct ScatterResult {
    // Per species spawn points (grid coordinates in [0..W) × [0..H))
    std::vector<std::vector<F2>> points;

    // Optional debug fields (all size W*H):
    std::vector<float> slope01;     // normalized slope 0..1
    std::vector<float> moisture01;  // 1 near water, ~0 far inland
    std::vector<float> lowland01;   // 1 close to sea level (on land), 0 high
};

// --------------------- Internals ---------------------

namespace detail {

inline int I(int x,int y,int W){ return y*W + x; }
inline bool inb(int x,int y,int W,int H){ return x>=0 && y>=0 && x<W && y<H; }
inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

static constexpr float PI = 3.14159265358979323846f;

struct Rand {
    std::mt19937_64 rng;
    explicit Rand(uint64_t s): rng(s) {}
    float uf(float a, float b) { std::uniform_real_distribution<float> d(a,b); return d(rng); }
    int   ui(int a, int b)     { std::uniform_int_distribution<int>    d(a,b); return d(rng); }
};

// Small value noise for gentle modulation (optional)
inline uint32_t mix32(uint32_t v){ v^=v>>16; v*=0x7feb352dU; v^=v>>15; v*=0x846ca68bU; v^=v>>16; return v; }
inline float hash01(uint32_t h){ return (mix32(h) & 0xFFFFFFu) / float(0xFFFFFFu); }
inline float vnoise(float x,float y,uint32_t seed){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float tx=x-xi, ty=y-yi;
    auto v=[&](int X,int Y){
        return hash01((uint32_t)(X*73856093u ^ Y*19349663u ^ (int)seed));
    };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    auto s = [](float t){return t*t*(3.f-2.f*t);};
    float a=v00+(v10-v00)*s(tx), b=v01+(v11-v01)*s(tx);
    return a+(b-a)*s(ty);
}

// Compute normalized slope from height01 (expects [0..1])
inline std::vector<float> slope01(const std::vector<float>& h, int W, int H, float metersPerUnit){
    std::vector<float> s((size_t)W*H, 0.f);
    auto Hs=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[(size_t)I(x,y,W)];
    };
    float maxg = 1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Hs(x+1,y)-Hs(x-1,y));
        float gy=0.5f*(Hs(x,y+1)-Hs(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy) * metersPerUnit;
        s[(size_t)I(x,y,W)] = g;
        if (g>maxg) maxg=g;
    }
    for(float& v : s) v /= maxg; // normalize to 0..1
    return s;
}

// Multi-source distance to water (8-neigh Dijkstra with uniform weights)
inline std::vector<float> distToWater(const std::vector<uint8_t>* waterMask,
                                      const std::vector<float>& h, int W, int H, float seaLevel)
{
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    std::vector<int> dx={0,1,1,1,0,-1,-1,-1};
    std::vector<int> dy={-1,-1,0,1,1,1,0,-1};
    std::vector<float> step={1,1.4142f,1,1.4142f,1,1.4142f,1,1.4142f};
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;

    auto isWater = [&](int x,int y){
        if (waterMask) return (*waterMask)[(size_t)I(x,y,W)]!=0;
        return h[(size_t)I(x,y,W)] < seaLevel;
    };

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        if (isWater(x,y)){ d[(size_t)I(x,y,W)]=0.f; pq.emplace(0.f, I(x,y,W)); }
    }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop();
        if (cd>d[(size_t)i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k];
            if(!inb(nx,ny,W,H)) continue;
            int j=I(nx,ny,W);
            float nd=cd+step[k];
            if (nd<d[(size_t)j]){ d[(size_t)j]=nd; pq.emplace(nd,j); }
        }
    }
    return d;
}

// ---------------- Bridson Poisson-disc (constant radius) ----------------
// (classic O(N) algorithm with background grid + active list)
struct Poisson {
    int W,H,gw,gh; float cell;
    int k; Rand* rnd; // candidate attempts, RNG
    std::vector<int> grid; // cell->sample index or -1
    std::vector<F2>  samples;
    std::vector<int> active;

    Poisson(int W_,int H_,float radius,int k_, Rand& r)
        : W(W_),H(H_),k(k_),rnd(&r)
    {
        cell = radius / std::sqrt(2.f);
        gw = std::max(1, (int)std::ceil(W/cell));
        gh = std::max(1, (int)std::ceil(H/cell));
        grid.assign((size_t)gw*gh, -1);
        samples.reserve((size_t)(W*H / (radius*radius)));
    }

    inline std::pair<int,int> toCell(const F2& p) const {
        int cx = std::clamp((int)std::floor(p.x / cell), 0, gw-1);
        int cy = std::clamp((int)std::floor(p.y / cell), 0, gh-1);
        return {cx,cy};
    }
    inline int gidx(int cx,int cy) const { return cy*gw+cx; }

    inline bool farEnough(const F2& p, float r2) const {
        auto [cx,cy]=toCell(p);
        for(int yy=std::max(0,cy-2); yy<=std::min(gh-1,cy+2); ++yy)
            for(int xx=std::max(0,cx-2); xx<=std::min(gw-1,cx+2); ++xx){
                int si = grid[(size_t)gidx(xx,yy)];
                if (si < 0) continue;
                const F2& q = samples[(size_t)si];
                float dx=p.x-q.x, dy=p.y-q.y;
                if (dx*dx+dy*dy < r2) return false;
            }
        return true;
    }

    inline void place(const F2& p, bool activate) {
        auto [cx,cy]=toCell(p);
        int idx = (int)samples.size();
        samples.push_back(p);
        grid[(size_t)gidx(cx,cy)] = idx;
        if (activate) active.push_back(idx);
    }

    // seed any boundary samples (do not activate; they only enforce spacing)
    void addBoundary(const std::vector<F2>& seeds, float radius2){
        for (auto& p : seeds){
            if (p.x<0||p.y<0||p.x>=W||p.y>=H) continue;
            auto [cx,cy]=toCell(p);
            if (grid[(size_t)gidx(cx,cy)] == -1 && farEnough(p, radius2))
                place(p,false);
        }
    }

    std::vector<F2> run(float radius, const std::vector<F2>& boundary = {}){
        float r2 = radius*radius;
        if (!boundary.empty()) addBoundary(boundary, r2);

        // first sample random
        F2 p0{ rnd->uf(0.f,(float)W), rnd->uf(0.f,(float)H) };
        place(p0,true);

        while(!active.empty()){
            int aidx = rnd->ui(0,(int)active.size()-1);
            int sidx = active[(size_t)aidx];
            F2 base = samples[(size_t)sidx];

            bool found=false;
            for(int t=0;t<k;++t){
                float r = rnd->uf(radius, 2.f*radius);
                float ang = rnd->uf(0.f, 2.f*PI);
                F2 cand { base.x + r*std::cos(ang), base.y + r*std::sin(ang) };
                if (cand.x<0||cand.y<0||cand.x>=W||cand.y>=H) continue;
                if (!farEnough(cand, r2)) continue;
                place(cand,true); found=true; break;
            }
            if (!found){ active[(size_t)aidx]=active.back(); active.pop_back(); }
        }
        return samples;
    }
};

// ---------------- Variable-radius Poisson-disc ----------------
// Uses min/max per species and a desirability(x,y) in [0..1], as is common in
// variable-density sampling literature; higher desirability -> smaller radius.
struct VDPoisson {
    int W,H,gw,gh; float cell; int k; Rand* rnd;
    std::vector<int> grid; std::vector<F2> samples; std::vector<int> active;

    VDPoisson(int W_,int H_, float minRadius, int k_, Rand& r)
        : W(W_),H(H_),k(k_),rnd(&r)
    {
        cell = std::max(1.f, minRadius / std::sqrt(2.f));
        gw = std::max(1, (int)std::ceil(W/cell));
        gh = std::max(1, (int)std::ceil(H/cell));
        grid.assign((size_t)gw*gh, -1);
    }
    inline std::pair<int,int> toCell(const F2& p) const {
        int cx = std::clamp((int)std::floor(p.x / cell), 0, gw-1);
        int cy = std::clamp((int)std::floor(p.y / cell), 0, gh-1);
        return {cx,cy};
    }
    inline int gidx(int cx,int cy) const { return cy*gw+cx; }

    inline void place(const F2& p, bool activate){
        auto [cx,cy]=toCell(p);
        int idx=(int)samples.size();
        samples.push_back(p);
        grid[(size_t)gidx(cx,cy)]=idx;
        if (activate) active.push_back(idx);
    }

    // Neighbor test with variable radii: we enforce min distance >= max(r_here, r_that)
    inline bool farEnough(const F2& p, float rHere,
                          const std::vector<float>& radii,
                          float minRadius) const
    {
        auto [cx,cy]=toCell(p);
        int R = (int)std::ceil((rHere / cell)) + 1;
        for(int yy=std::max(0,cy-R); yy<=std::min(gh-1,cy+R); ++yy)
            for(int xx=std::max(0,cx-R); xx<=std::min(gw-1,cx+R); ++xx){
                int si=grid[(size_t)gidx(xx,yy)];
                if (si<0) continue;
                const F2& q = samples[(size_t)si];
                float req = std::max(rHere, radii[(size_t)si]);
                float dx=p.x-q.x, dy=p.y-q.y;
                if (dx*dx+dy*dy < req*req) return false;
            }
        return true;
    }

    inline float desirabilityToRadius(float desirability,
                                      float minR, float maxR, float noise)
    {
        // Map desirability (higher better) to radius: smaller in better spots.
        // Ensure stable [minR, maxR] bounds; blend a little noise for texture.
        float d = clamp01(desirability);
        float r = maxR - d * (maxR - minR);
        r *= (0.9f + 0.2f*noise); // ±10% modulation
        return std::clamp(r, minR, maxR);
    }

    std::vector<F2> run(float minR, float maxR,
                        const std::vector<float>& desirability01,
                        const std::vector<F2>& boundary = {})
    {
        // Precompute per-sample radii as we place; we store radii in a parallel vector
        std::vector<float> radii; radii.reserve(2048);

        // Place boundary seeds (if any). For variable-r, we approximate radius at that point.
        for(const auto& p : boundary){
            if (p.x<0||p.y<0||p.x>=W||p.y>=H) continue;
            size_t idx2 = (size_t)I((int)p.x,(int)p.y,W);
            float d = desirability01[idx2];
            float n = vnoise(p.x*0.037f, p.y*0.037f, 12345u);
            float r = desirabilityToRadius(d, minR, maxR, n);
            auto [cx,cy]=toCell(p);
            int gi=gidx(cx,cy);
            if (grid[(size_t)gi]==-1){
                samples.push_back(p);
                grid[(size_t)gi] = (int)radii.size();
                radii.push_back(r);
            }
        }

        // First active sample: random position
        F2 p0{ rnd->uf(0.f,(float)W), rnd->uf(0.f,(float)H) };
        size_t i0 = (size_t)I((int)p0.x,(int)p0.y,W);
        float d0  = desirability01[i0];
        float n0  = vnoise(p0.x*0.037f, p0.y*0.037f, 6789u);
        float r0  = desirabilityToRadius(d0, minR, maxR, n0);
        place(p0,true); radii.push_back(r0);

        while(!active.empty()){
            int aidx = rnd->ui(0,(int)active.size()-1);
            int sidx = active[(size_t)aidx];
            F2 base = samples[(size_t)sidx];

            bool found=false;
            for(int t=0; t<k; ++t){
                float stepR = radii[(size_t)sidx];
                float r = rnd->uf(stepR, 2.f*stepR);
                float ang = rnd->uf(0.f, 2.f*PI);
                F2 cand{ base.x + r*std::cos(ang), base.y + r*std::sin(ang) };
                if (cand.x<0||cand.y<0||cand.x>=W||cand.y>=H) continue;

                size_t ci = (size_t)I((int)cand.x,(int)cand.y,W);
                float d   = desirability01[ci];
                float n   = vnoise(cand.x*0.037f, cand.y*0.037f, 42u);
                float rHere = desirabilityToRadius(d, minR, maxR, n);

                if (!farEnough(cand, rHere, radii, minR)) continue;

                // Accept
                auto [cx,cy]=toCell(cand);
                grid[(size_t)gidx(cx,cy)] = (int)samples.size();
                samples.push_back(cand);
                radii.push_back(rHere);
                active.push_back((int)samples.size()-1);
                found=true; break;
            }
            if (!found){ active[(size_t)aidx]=active.back(); active.pop_back(); }
        }
        return samples;
    }
};

} // namespace detail

// --------------------- Main entry point ---------------------

// height01: W×H floats in [0..1] (sea in [0, seaLevel))
// waterMask: optional W×H bytes (1 = water)
// species: list of species with spacing + weighting
inline ScatterResult ScatterVegetation(const std::vector<float>& height01,
                                       int W, int H,
                                       const std::vector<uint8_t>* waterMask,
                                       const std::vector<SpeciesParams>& species,
                                       const ScatterParams& P = {})
{
    ScatterResult out;
    out.points.resize(species.size());
    if (W<=1 || H<=1 || (int)height01.size()!=W*H || species.empty()) return out;

    // --- Terrain fields ---
    out.slope01    = detail::slope01(height01, W, H, P.metersPerHeightUnit);
    auto d2w       = detail::distToWater(waterMask, height01, W, H, P.seaLevel);

    out.moisture01.resize((size_t)W*H);
    out.lowland01.resize((size_t)W*H);

    // Normalize distance to water -> moisture in [0..1] (closer = wetter)
    float maxd = 1e-6f;
    for(float v : d2w) if (v< std::numeric_limits<float>::infinity() && v>maxd) maxd=v;
    for(size_t i=0;i<out.moisture01.size();++i){
        float t = (std::isinf(d2w[i])? maxd : d2w[i]) / std::max(1e-6f,maxd);
        out.moisture01[i] = 1.0f - std::clamp(t, 0.0f, 1.0f);
    }
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=(size_t)detail::I(x,y,W);
        float elev = height01[i];
        if (elev < P.seaLevel) out.lowland01[i] = 0.0f;
        else {
            float above = (elev - P.seaLevel) / std::max(1e-6f,(1.0f - P.seaLevel));
            out.lowland01[i] = 1.0f - std::clamp(above, 0.0f, 1.0f);
        }
    }

    // --- For each species, build a desirability map and run variable-radius PD ---
    detail::Rand rng(P.seed);

    for (size_t si=0; si<species.size(); ++si){
        const auto& S = species[si];
        std::vector<float> desirability((size_t)W*H, 0.f);

        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            size_t i=(size_t)detail::I(x,y,W);
            float m = out.moisture01[i];
            float flat = 1.0f - out.slope01[i];
            float low  = out.lowland01[i];

            float d = S.wMoisture*m + S.wFlat*flat + S.wLowland*low;

            // Keep land-only by default; allow sparse presence on beaches if desired
            if (height01[i] < P.seaLevel) d = 0.0f;

            // Gentle texture so forests aren't perfectly uniform
            float n = 0.15f * detail::vnoise(x*0.05f, y*0.05f, (uint32_t)(P.seed ^ (si*911u)));
            d = std::max(0.f, d * S.densityBoost + n);

            // Clamp out poor areas entirely
            d = (d >= S.minPresence) ? d : 0.0f;

            desirability[i] = detail::clamp01(d);
        }

        // Run variable-radius Poisson-disk using desirability->radius mapping
        detail::VDPoisson vpd(W,H, S.minRadius, P.kCandidates, rng);
        auto pts = vpd.run(S.minRadius, S.maxRadius, desirability, P.boundarySeeds);
        out.points[si] = std::move(pts);
    }

    return out;
}

/*
----------------------------------- Usage -----------------------------------

#include "worldgen/VegetationScatter.hpp"

// Example: two tree species + one rock scatter
void placeVegetation(const std::vector<float>& height01, int W, int H,
                     const std::vector<uint8_t>* waterMask)
{
    using namespace worldgen;

    std::vector<SpeciesParams> species = {
        // Dense riparian trees: likes water & flat ground
        SpeciesParams{ "RiparianTree", 7.0f, 14.0f, 0.70f, 0.25f, 0.05f, 1.1f, 0.10f },
        // Upland pines: less moisture bias, tolerates slope a bit more
        SpeciesParams{ "Pine",         9.0f, 18.0f, 0.40f, 0.45f, 0.15f, 1.0f, 0.05f },
        // Rocks: prefer slopes; invert weights by adjusting wFlat low and densityBoost high
        SpeciesParams{ "Rocks",       10.0f, 22.0f, 0.10f, 0.10f, 0.00f, 1.2f, 0.00f }
    };

    ScatterParams P; P.seed = 0xBEEFCAFEu; P.kCandidates = 28; P.seaLevel = 0.50f;

    ScatterResult R = ScatterVegetation(height01, W, H, waterMask, species, P);

    // Spawn them into the world (your code):
    for (size_t s=0; s<R.points.size(); ++s) {
        for (const F2& p : R.points[s]) {
            world.spawn(species[s].name, (int)std::round(p.x), (int)std::round(p.y));
        }
    }

    // Optional: use R.slope01 / R.moisture01 for debugging overlays.
}
*/
} // namespace worldgen
