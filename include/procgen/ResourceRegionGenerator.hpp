#pragma once
// ============================================================================
// ResourceRegionGenerator.hpp — biome-aware resource regions + densities
// Header-only, C++17, STL only. Deterministic via seed.
// Produces per-cell density maps for 5 resource types and optional seed points.
//
// Resources (fixed set, simple to extend):
//   0 = FOREST, 1 = IRON, 2 = STONE, 3 = CLAY, 4 = GAME (wildlife)
//
// Inputs:
//   • height01: W×H float array in [0,1] (sea in [0, seaLevel))
//   • optional external water mask (W×H bytes; 1 = water)
//   • tunable parameters below
//
// Techniques (background reading; implementation is original):
//   • Blue-noise/Poisson-like sampling with grid accelerator          [Bridson 2007]
//   • Weighted Voronoi / Power diagram for region sizing               [Aurenhammer]
//   • 1–2 Lloyd relax iterations toward centroidal Voronoi tessellations [Du et al.]
//   • fBM (fractal Brownian motion) for organic intra-region texture
// ============================================================================

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>

namespace procgen {

// ----------------------------- Public API -----------------------------

enum ResourceType : uint8_t { FOREST=0, IRON=1, STONE=2, CLAY=3, GAME=4, RESOURCE_COUNT=5 };

struct RRParams {
    int      width = 512, height = 512;
    uint64_t seed  = 20231117ull;

    // Water / terrain
    float    seaLevel = 0.50f;
    const uint8_t* externalWater = nullptr; // optional W×H (1 = water)
    bool     clampWaterUnchanged = true;    // keep densities 0 over water, except GAME

    // Region counts & sizes
    int      regionCount = 28;              // total regions to seed
    float    minSpacing  = 18.0f;           // Poisson-like spacing in cells
    float    maxSpacing  = 34.0f;           // upper bound (used for variable spacing)
    float    spacingGamma= 0.8f;            // bias spacing by site score (<1 packs in good areas)

    // Relative target area by resource (used as power weights: bigger → larger regions)
    std::array<float, RESOURCE_COUNT> targetArea = { 1.8f, 0.6f, 0.9f, 0.8f, 0.9f };

    // Per-resource environmental taste (simple heuristics 0..1)
    // These shape the *site score* for seeding and the *density* later.
    float forest_water_pref   = 0.65f;  // closer to water helps
    float forest_slope_avoid  = 0.65f;  // flatter helps
    float clay_water_pref     = 0.80f;  // clay loves low, wet places
    float stone_slope_pref    = 0.55f;  // stony fields on broken terrain
    float iron_highland_pref  = 0.55f;  // iron belts a bit inland/high
    float game_edge_pref      = 0.55f;  // wildlife near forest edges (handled via fBM mix)

    // fBM shaping
    int   fbm_octaves = 4;
    float fbm_gain    = 0.5f;
    float fbm_lacunarity = 2.0f;
    float fbm_scale   = 0.045f; // world→noise scale

    // Lloyd relaxation (0..2 steps usually enough)
    int   lloyd_iters = 1;

    // Output postprocess
    float global_density = 1.0f;  // multiply all densities (0..1)
};

struct RRResult {
    int width=0, height=0;

    // Densities per resource; length = W*H; 0..1 (clamped)
    std::array<std::vector<float>, RESOURCE_COUNT> density;

    // Optional: region seed points (after relaxation)
    struct Seed { int x, y; ResourceType type; float weight; };
    std::vector<Seed> seeds;

    // The water mask actually used (for convenience)
    std::vector<uint8_t> waterMask;
};

// ----------------------------- Internal helpers -----------------------------

namespace detail {

inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool   inb(int x,int y,int W,int H){ return x>=0 && y>=0 && x<W && y<H; }
inline float  saturate(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

// ---------- Hash / value-noise / fBM (simple, fast) ----------

inline uint32_t mix32(uint32_t x){
    x ^= x >> 17; x *= 0xed5ad4bbU; x ^= x >> 11; x *= 0xac4c1b51U; x ^= x >> 15; x *= 0x31848babU; x ^= x >> 14;
    return x;
}
inline float hash01(uint32_t x){ return (mix32(x) >> 8) * (1.0f/16777216.0f); } // [0,1)

inline float value_noise2f(float x, float y, uint32_t seed){
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float tx = x - xi, ty = y - yi;

    auto v = [&](int X,int Y){
        return hash01((uint32_t)(X*73856093u ^ Y*19349663u) ^ seed);
    };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    auto smooth = [](float t){ return t*t*(3.0f-2.0f*t); };
    float a = v00 + (v10 - v00)*smooth(tx);
    float b = v01 + (v11 - v01)*smooth(tx);
    return a + (b - a)*smooth(ty);
}
inline float fbm2(float x,float y,int oct,float lac,float gain,uint32_t seed){
    float f=0.0f, amp=1.0f;
    for(int i=0;i<oct;++i){
        f += amp * value_noise2f(x,y,seed + (uint32_t)i*1013u);
        x *= lac; y *= lac; amp *= gain;
    }
    return f / ((1.0f - std::pow(gain, (float)oct)) / (1.0f - gain));
}

// ---------- Terrain fields ----------

inline std::vector<uint8_t> derive_water(const float* h, int W,int H,float seaLevel){
    std::vector<uint8_t> w((size_t)W*H,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x)
        w[idx(x,y,W)] = (h[idx(x,y,W)] < seaLevel) ? 1 : 0;
    return w;
}

inline std::vector<float> slope_grid(const float* h,int W,int H){
    std::vector<float> s((size_t)W*H,0.0f);
    auto Ht=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[idx(x,y,W)];
    };
    float maxv=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Ht(x+1,y)-Ht(x-1,y));
        float gy=0.5f*(Ht(x,y+1)-Ht(x,y-1));
        float g=std::sqrt(gx*gx+gy*gy);
        s[idx(x,y,W)]=g; if(g>maxv) maxv=g;
    }
    for(float& v: s) v/=maxv; // [0,1]
    return s;
}

// Fast multi-source distance-to-water (8-neighborhood Dijkstra with uniform weights)
inline std::vector<float> distance_to_mask(const std::vector<uint8_t>& mask,int W,int H){
    const size_t N=(size_t)W*H;
    std::vector<float> d(N, std::numeric_limits<float>::infinity());
    using Node=std::pair<float,int>;
    std::vector<int> dx={0,1,1,1,0,-1,-1,-1};
    std::vector<int> dy={-1,-1,0,1,1,1,0,-1};
    std::vector<float> step={1,1.4142f,1,1.4142f,1,1.4142f,1,1.4142f};

    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> pq;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i=idx(x,y,W);
        if(mask[i]){ d[i]=0.0f; pq.emplace(0.0f,(int)i); }
    }
    while(!pq.empty()){
        auto [cd,i]=pq.top(); pq.pop();
        if(cd>d[i]) continue;
        int x=i%W, y=i/W;
        for(int k=0;k<8;++k){
            int nx=x+dx[k], ny=y+dy[k];
            if(!inb(nx,ny,W,H)) continue;
            size_t j=idx(nx,ny,W);
            float nd=cd+step[k];
            if(nd<d[j]){ d[j]=nd; pq.emplace(nd,(int)j); }
        }
    }
    return d;
}

// ---------- Poisson-like sampler (grid-accelerated) ----------

struct Sampler {
    int W,H,gw,gh; float cell, rmin, rmax, gamma;
    std::vector<int> grid; // cell → chosen index or -1
    Sampler(int W_,int H_,float rmin_,float rmax_,float g_)
        : W(W_),H(H_),rmin(rmin_),rmax(rmax_),gamma(g_)
    {
        cell = std::max(1.0f, rmin / std::sqrt(2.0f));
        gw = std::max(1, (int)std::ceil(W / cell));
        gh = std::max(1, (int)std::ceil(H / cell));
        grid.assign((size_t)gw*gh, -1);
    }
    inline std::pair<int,int> toCell(int x,int y) const {
        int cx=std::clamp((int)std::floor(x / cell), 0, gw-1);
        int cy=std::clamp((int)std::floor(y / cell), 0, gh-1);
        return {cx,cy};
    }
    inline float radiusFromScore(float s) const {
        float t=std::pow(saturate(s), gamma);
        return std::clamp(rmax*(1.0f - t) + rmin*t, rmin, rmax);
    }
    bool farEnough(const std::vector<std::pair<int,int>>& pts,int x,int y,float r) const {
        auto [cx,cy]=toCell(x,y);
        int R=(int)std::ceil(r/cell)+1;
        for(int yy=std::max(0,cy-R); yy<=std::min(gh-1, cy+R); ++yy)
            for(int xx=std::max(0,cx-R); xx<=std::min(gw-1, cx+R); ++xx){
                int at=grid[(size_t)yy*gw+xx];
                if(at<0) continue;
                auto [px,py]=pts[(size_t)at];
                float dx=float(px-x), dy=float(py-y);
                if(dx*dx+dy*dy < r*r) return false;
            }
        return true;
    }
    void insert(const std::pair<int,int>& p,int index){
        auto [cx,cy]=toCell(p.first,p.second);
        grid[(size_t)cy*gw+cx]=index;
    }
};

// ---------- Region seed & assignment ----------

struct Seed {
    int x=0,y=0; ResourceType type=FOREST; float weight=1.0f;
};

inline float site_score(ResourceType t, float elev, float slope, float d2w,
                        const RRParams& P)
{
    // Simple, monotone "tastes" per type; combine and clamp to [0,1]
    float s=0.0f;
    switch(t){
        case FOREST: {
            float nearW = std::exp(-0.20f * d2w);
            float flat  = 1.0f - slope;
            s = P.forest_water_pref * nearW + P.forest_slope_avoid * flat;
        } break;
        case CLAY: {
            float low   = 1.0f - std::tanh(std::max(0.0f, elev - (P.seaLevel+0.08f))*6.0f);
            float nearW = std::exp(-0.18f * d2w);
            s = 0.5f*(P.clay_water_pref*nearW + low);
        } break;
        case STONE: {
            float rough = slope;
            s = P.stone_slope_pref * rough;
        } break;
        case IRON: {
            float midHigh = std::clamp((elev - (P.seaLevel+0.12f))*2.2f, 0.0f,1.0f);
            float awayW   = 1.0f - std::exp(-0.10f * d2w);
            s = 0.5f*(midHigh + P.iron_highland_pref*awayW);
        } break;
        case GAME: {
            // prefer near forest edges later; here just "not too steep, not underwater"
            float gentle = 1.0f - slope;
            float someW  = 0.4f * std::exp(-0.10f*d2w);
            s = 0.6f*gentle + someW;
        } break;
        default: break;
    }
    return saturate(s);
}

inline std::vector<Seed> seed_regions(const float* h,
                                      const std::vector<float>& slope,
                                      const std::vector<float>& d2w,
                                      int W,int H,const RRParams& P)
{
    // Decide how many seeds per resource proportional to targetArea
    float sum=0.f; for(float v: P.targetArea) sum += v;
    std::array<int, RESOURCE_COUNT> per{};
    int leftover = P.regionCount;
    for(int t=0;t<RESOURCE_COUNT;++t){
        per[t] = std::max(0, (int)std::round(P.regionCount * (P.targetArea[(size_t)t] / std::max(1e-6f,sum))));
        leftover -= per[t];
    }
    // Distribute leftovers to largest targetArea
    while(leftover-- > 0){
        int best = std::distance(P.targetArea.begin(),
                     std::max_element(P.targetArea.begin(), P.targetArea.end()));
        per[best]++;
    }

    // Score field per type for variable spacing
    std::array<std::vector<float>, RESOURCE_COUNT> score;
    for(int t=0;t<RESOURCE_COUNT;++t){
        score[(size_t)t].resize((size_t)W*H);
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            size_t i=idx(x,y,W);
            score[(size_t)t][i] = site_score((ResourceType)t, h[i], slope[i], d2w[i], P);
            if (h[i] < P.seaLevel && (t!=GAME)) score[(size_t)t][i] = 0.f; // keep non-water on land
        }
    }

    std::mt19937_64 rng(P.seed);
    std::vector<Seed> seeds; seeds.reserve((size_t)P.regionCount);

    for(int t=0;t<RESOURCE_COUNT;++t){
        if (per[t]==0) continue;
        detail::Sampler sampler(W,H, P.minSpacing, P.maxSpacing, P.spacingGamma);
        std::vector<std::pair<int,int>> chosen;

        // Greedy: loop cells by descending score and accept if far enough
        std::vector<int> order((size_t)W*H); for(size_t i=0;i<order.size();++i) order[i]=(int)i;
        std::sort(order.begin(), order.end(), [&](int a,int b){
            return score[(size_t)t][(size_t)a] > score[(size_t)t][(size_t)b];
        });

        for(int idxi : order){
            if ((int)chosen.size() >= per[t]) break;
            float s = score[(size_t)t][(size_t)idxi];
            if (s < 0.25f) break; // don't seed in terrible spots
            int x=idxi%W, y=idxi/W;

            float r = sampler.radiusFromScore(s);
            if (!sampler.farEnough(chosen, x,y,r)) continue;

            chosen.emplace_back(x,y);
            sampler.insert(chosen.back(), (int)chosen.size()-1);

            // weight scales region "power" (larger → bigger territory)
            float w = std::max(0.25f, P.targetArea[(size_t)t]);
            seeds.push_back({x,y,(ResourceType)t,w});
        }
    }
    return seeds;
}

// Assign each cell to the seed minimizing (d^2 - weight^2)  — a power diagram
inline std::vector<int> assign_regions_power(const std::vector<Seed>& seeds,
                                             int W,int H)
{
    const size_t N=(size_t)W*H;
    std::vector<int> owner(N, -1);
    std::vector<float> best(N, std::numeric_limits<float>::infinity());

    for(size_t si=0; si<seeds.size(); ++si){
        const Seed& s = seeds[si];
        float wsq = s.weight*s.weight;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            size_t i=idx(x,y,W);
            float dx=float(x-s.x), dy=float(y-s.y);
            float d = dx*dx + dy*dy - wsq;
            if (d < best[i]){ best[i]=d; owner[i]=(int)si; }
        }
    }
    return owner;
}

// one Lloyd step: move seed to centroid of its region
inline void lloyd_relax(std::vector<Seed>& seeds, const std::vector<int>& owner, int W,int H){
    const size_t S = seeds.size();
    std::vector<double> sumx(S,0), sumy(S,0), cnt(S,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        int o = owner[(size_t)idx(x,y,W)];
        if (o<0) continue;
        sumx[(size_t)o]+=x; sumy[(size_t)o]+=y; cnt[(size_t)o]+=1.0;
    }
    for(size_t i=0;i<S;++i){
        if (cnt[i] > 0.0){
            seeds[i].x = (int)std::round(sumx[i]/cnt[i]);
            seeds[i].y = (int)std::round(sumy[i]/cnt[i]);
        }
    }
}

} // namespace detail

// ----------------------------- Entry point -----------------------------

inline RRResult GenerateResourceRegions(const float* height01, int W, int H, const RRParams& P)
{
    RRResult out; out.width=W; out.height=H;
    if (W<=0 || H<=0 || !height01) return out;

    // Terrain helpers
    out.waterMask = P.externalWater
        ? std::vector<uint8_t>((size_t)W*H)
        : detail::derive_water(height01, W, H, P.seaLevel);

    if (P.externalWater) {
        for (int y=0;y<H;++y) for (int x=0;x<W;++x)
            out.waterMask[detail::idx(x,y,W)] = P.externalWater[detail::idx(x,y,W)] ? 1 : 0;
    }

    auto slope = detail::slope_grid(height01, W, H);
    auto d2w   = detail::distance_to_mask(out.waterMask, W, H);

    // 1) Seed regions per resource type (blue-noise spacing; power weights from targetArea)
    auto seeds = detail::seed_regions(height01, slope, d2w, W, H, P);

    // 2) Region assignment (power diagram), optional Lloyd smoothing
    std::vector<int> owner = detail::assign_regions_power(seeds, W, H);
    for(int it=0; it<P.lloyd_iters; ++it){
        detail::lloyd_relax(seeds, owner, W, H);
        owner = detail::assign_regions_power(seeds, W, H);
    }
    out.seeds.reserve(seeds.size());
    for(const auto& s : seeds) out.seeds.push_back({s.x,s.y,s.type,s.weight});

    // 3) Build per-resource densities using environment taste × fBM inside region
    for(int t=0;t<RESOURCE_COUNT;++t) out.density[(size_t)t].assign((size_t)W*H, 0.0f);

    std::mt19937_64 rng(P.seed ^ 0xC0FFEE1234ULL);
    uint32_t nseed = (uint32_t)rng();

    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        size_t i = detail::idx(x,y,W);
        int sIdx = owner[i]; if (sIdx<0) continue;

        const auto& s = seeds[(size_t)sIdx];
        ResourceType t = s.type;

        float elev  = height01[i];
        float sl    = slope[i];
        float nearW = std::exp(-0.15f * d2w[i]);

        // Base environmental weight (0..1)
        float base = detail::site_score(t, elev, sl, d2w[i], P);

        // fBM shaping: same scale across map, but each resource type gets its own seed space
        float nx = x * P.fbm_scale, ny = y * P.fbm_scale;
        float f  = detail::fbm2(nx + 17.0f*(int)t, ny - 23.0f*(int)t, P.fbm_octaves, P.fbm_lacunarity, P.fbm_gain, nseed + (uint32_t)(t*4099));

        // Region soft mask using distance to seed (radial falloff) so interior is denser
        float dx = float(x - s.x), dy = float(y - s.y);
        float r2 = dx*dx + dy*dy;
        float fall = 1.0f / (1.0f + 0.0015f*r2); // gentle 1/r^2-ish

        float d = base * (0.55f + 0.45f * f) * fall;

        // Tiny type-specific tweaks
        if (t==GAME)  d = std::min(1.0f, d * (0.6f + 0.4f*(1.0f - sl))); // hates steep
        if (t==CLAY)  d = std::min(1.0f, d * (0.6f + 0.6f*nearW));
        if (t==STONE) d = std::min(1.0f, d * (0.6f + 0.5f*sl));

        // Respect water (except GAME can have small density near coasts/islands)
        if (P.clampWaterUnchanged && out.waterMask[i]) {
            if (t==GAME) d *= 0.25f; else d = 0.0f;
        }

        out.density[(size_t)t][i] = detail::saturate(d * P.global_density);
    }

    return out;
}

} // namespace procgen
