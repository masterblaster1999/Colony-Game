#pragma once
// ============================================================================
// OreVeinGenerator.hpp — geology-inspired ore & quarry generator for 2D grids
// C++17 / STL-only  |  MIT (or match the project's license)
//
// Generates:
//  • density[IRON], density[COPPER], density[GOLD] — hydrothermal-style lodes
//  • density[COAL] — stratiform seams warped by gentle folding
//  • density[STONE] — quarry-grade rock (exposed, competent)
//  • lodes — polylines (vein centerlines) for rendering/inspection/POIs
//  • debug slope01
//
// Inputs:
//  • height01 (normalized [0..1]); optional water mask
//
// Notes:
//  • Veins follow wandering fault-like polylines; grade falls with distance
//    and is modulated by fBM noise + terrain exposure.
//  • Coal seams are simple warped horizons favored in lowlands and gentle slopes.
// ============================================================================

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

namespace procgen {

// ----------------------------- Public API -----------------------------

enum OreType : uint8_t { IRON=0, COPPER=1, GOLD=2, COAL=3, STONE=4, ORE_COUNT=5 };

struct VeinPolyline {
    std::vector<std::pair<float,float>> points; // grid coords
    OreType type = IRON;
};

struct OreParams {
    int      width = 0, height = 0;
    uint64_t seed  = 0x5EEDBEEF1234ULL;

    // Sea & terrain interpretation
    float sea_level = 0.50f;            // height <= sea_level is water (unless water mask says otherwise)
    float meters_per_height_unit = 1200.0f;

    // -------- Fault-fracture "lineaments" (for lodes) --------
    int   fault_count = 36;             // number of major lineaments
    float regional_strike_deg = 35.0f;  // average strike (deg, 0°=+x)
    float strike_jitter_deg   = 18.0f;  // ± strike deviation
    int   fault_min_len       = 80;     // polyline length in segments
    int   fault_max_len       = 220;
    float fault_wander        = 12.0f;  // heading wander (deg)
    float branch_probability  = 0.18f;  // chance to add a short branch
    float branch_len_frac     = 0.35f;  // branch length relative to parent
    int   max_segments_per_fault = 600; // safety cap

    // Lode shaping
    float lode_half_thickness = 2.8f;   // half-width of vein (cells)
    float lode_falloff_power  = 1.6f;   // profile sharpness
    float lode_noise_amp      = 0.35f;  // grade/thickness modulation (fBM)

    // Ore mix along faults (weights ~sum to 1)
    float w_iron   = 0.46f;
    float w_copper = 0.38f;
    float w_gold   = 0.16f;

    // Terrain correlation
    float slope_bias   = 0.35f;         // more exposure on steeper ground
    float upland_bias  = 0.25f;         // boost above sea

    // -------- Stratiform seams (coal) --------
    int   seam_count       = 3;
    float seam_thickness   = 1.8f;      // half-thickness (cells) in elevation space
    float seam_fold_amp    = 5.0f;      // cells
    float seam_fold_scale  = 0.015f;    // 1/cell
    float seam_basin_bias  = 0.55f;     // favor lowlands
    float seam_slope_avoid = 0.40f;     // avoid steep slopes

    // -------- Stone (quarry) mask --------
    float stone_from_slope = 0.55f;     // exposure via slope
    float stone_from_height= 0.25f;     // exposure via elevation

    // Water handling
    bool  suppress_under_sea    = true;
    float underwater_multiplier = 0.2f;
};

struct OreResult {
    int width=0, height=0;
    std::array<std::vector<float>, ORE_COUNT> density; // per-ore 0..1
    std::vector<VeinPolyline> lodes;
    std::vector<float> slope01; // debug
};

// ----------------------------- Internals -----------------------------
namespace detail {

inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return x>=0 && y>=0 && x<W && y<H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
inline float lerp(float a,float b,float t){ return a + (b-a)*t; }

struct RNG {
    std::mt19937_64 g;
    explicit RNG(uint64_t s): g(s) {}
    float uf(float a,float b){ std::uniform_real_distribution<float> d(a,b); return d(g); }
    int   ui(int a,int b){ std::uniform_int_distribution<int> d(a,b); return d(g); }
    bool  chance(float p){ std::bernoulli_distribution d(std::clamp(p,0.f,1.f)); return d(g); }
};

// ----- small value noise + fBM -----
inline uint32_t mix32(uint32_t v){ v^=v>>16; v*=0x7feb352dU; v^=v>>15; v*=0x846ca68bU; v^=v>>16; return v; }
inline float hash01(uint32_t x){ return (mix32(x) & 0xFFFFFFu) / float(0xFFFFFFu); }
inline float vnoise(float x,float y,uint32_t seed){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float tx=x-xi, ty=y-yi;
    auto v=[&](int X,int Y){ return hash01(uint32_t(X*73856093u ^ Y*19349663u ^ (int)seed)); };
    auto s=[](float t){ return t*t*(3.f-2.f*t); };
    float v00=v(xi,yi), v10=v(xi+1,yi), v01=v(xi,yi+1), v11=v(xi+1,yi+1);
    float a=v00+(v10-v00)*s(tx), b=v01+(v11-v01)*s(tx);
    return a+(b-a)*s(ty);
}
inline float fbm2(float x,float y,int oct,float lac,float gain,uint32_t seed){
    float f=0.0f, amp=1.0f, norm=0.0f;
    for(int i=0;i<oct;++i){ f += amp * vnoise(x,y,seed + 1013u*i); norm += amp; x*=lac; y*=lac; amp*=gain; }
    return (norm>0.f)? f/norm : 0.f;
}

// normalized slope (0..1)
inline std::vector<float> slope01(const std::vector<float>& h,int W,int H,float metersPerUnit){
    std::vector<float> s((size_t)W*H, 0.f);
    auto Ht=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[idx(x,y,W)];
    };
    float maxg=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float gx=0.5f*(Ht(x+1,y)-Ht(x-1,y))*metersPerUnit;
        float gy=0.5f*(Ht(x,y+1)-Ht(x,y-1))*metersPerUnit;
        float g=std::sqrt(gx*gx+gy*gy);
        s[idx(x,y,W)]=g; if(g>maxg) maxg=g;
    }
    for(float& v : s) v/=maxg;
    return s;
}

// distance^2 from point to segment
inline float dist2_seg(float px,float py, float ax,float ay, float bx,float by){
    float vx=bx-ax, vy=by-ay; float wx=px-ax, wy=py-ay;
    float c1=vx*wx + vy*wy;
    if (c1 <= 0) { float dx=px-ax, dy=py-ay; return dx*dx + dy*dy; }
    float c2=vx*vx + vy*vy;
    if (c2 <= 0) { float dx=px-ax, dy=py-ay; return dx*dx + dy*dy; }
    float t=c1/c2;
    float qx=ax + t*vx, qy=ay + t*vy;
    float dx=px-qx, dy=py-qy;
    return dx*dx + dy*dy;
}
inline float dist_polyline(float px,float py, const std::vector<std::pair<float,float>>& P){
    if (P.empty()) return std::numeric_limits<float>::infinity();
    if (P.size()<2){ float dx=px-P.front().first, dy=py-P.front().second; return dx*dx+dy*dy; }
    float best=std::numeric_limits<float>::infinity();
    for(size_t i=0;i+1<P.size();++i){
        auto [ax,ay]=P[i], [bx,by]=P[i+1];
        float d2 = dist2_seg(px,py, ax,ay, bx,by);
        if (d2<best) best=d2;
    }
    return best;
}

// wandering polyline with small heading noise
inline std::vector<std::pair<float,float>> make_fault_line(
        int W,int H,int minLen,int maxLen,float strike_deg,float wander_deg,
        RNG& rng, int max_segments)
{
    const float toRad = 3.1415926535f/180.f;
    int steps = std::min(max_segments, rng.ui(minLen,maxLen));
    float x = rng.uf(5.f,(float)(W-6));
    float y = rng.uf(5.f,(float)(H-6));
    float heading = strike_deg * toRad;

    std::vector<std::pair<float,float>> line;
    line.reserve((size_t)steps+1);
    line.emplace_back(x,y);

    for(int i=0;i<steps;++i){
        float dtheta = rng.uf(-wander_deg, +wander_deg) * toRad * 0.25f;
        heading += dtheta;
        float step = rng.uf(0.8f, 1.3f);
        x += step * std::cos(heading);
        y += step * std::sin(heading);
        if (x<1 || y<1 || x>W-2 || y>H-2) break;
        line.emplace_back(x,y);
    }
    if (line.size()<2) line.emplace_back(x+std::cos(heading), y+std::sin(heading));
    return line;
}

} // namespace detail

// ----------------------------- Entry point -----------------------------

inline OreResult GenerateOre(const std::vector<float>& height01,
                             int W, int H,
                             const OreParams& P = {},
                             const std::vector<uint8_t>* waterMask /*optional*/)
{
    OreResult out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    for (int t=0;t<ORE_COUNT;++t) out.density[(size_t)t].assign(N, 0.0f);
    if (W<=1 || H<=1 || height01.size()!=N) return out;

    out.slope01 = detail::slope01(height01, W, H, P.meters_per_height_unit);

    auto is_water = [&](size_t i)->bool {
        if (waterMask) return (*waterMask)[i]!=0;
        return height01[i] <= P.sea_level;
    };

    auto& iron   = out.density[(size_t)IRON];
    auto& copper = out.density[(size_t)COPPER];
    auto& gold   = out.density[(size_t)GOLD];
    auto& coal   = out.density[(size_t)COAL];
    auto& stone  = out.density[(size_t)STONE];

    // ----- 1) Build multi-scale fault/lineament set -----
    detail::RNG rng(P.seed);
    float baseStrike = P.regional_strike_deg;

    auto pick_type = [&](detail::RNG& rr)->OreType{
        float r = rr.uf(0.f,1.f);
        if (r < P.w_iron) return IRON;
        r -= P.w_iron;
        if (r < P.w_copper) return COPPER;
        return GOLD;
    };

    std::vector<VeinPolyline> lodes;
    lodes.reserve((size_t)P.fault_count*2);

    for (int f=0; f<P.fault_count; ++f){
        float strike = baseStrike + rng.uf(-P.strike_jitter_deg, +P.strike_jitter_deg);
        auto line = detail::make_fault_line(W,H, P.fault_min_len, P.fault_max_len,
                                            strike, P.fault_wander, rng, P.max_segments_per_fault);
        // occasional branch
        if (rng.chance(P.branch_probability)) {
            auto branch = detail::make_fault_line(
                W,H,
                (int)std::round(P.fault_min_len*P.branch_len_frac),
                (int)std::round(P.fault_max_len*P.branch_len_frac),
                strike + rng.uf(-35.f, +35.f), P.fault_wander*1.2f, rng, P.max_segments_per_fault/2
            );
            if (!line.empty() && !branch.empty()){
                size_t attach = (size_t)rng.ui(0, (int)line.size()-1);
                auto off = line[attach];
                for (auto& p : branch){
                    p.first  += (off.first  - branch.front().first);
                    p.second += (off.second - branch.front().second);
                }
                VeinPolyline vb; vb.points = std::move(branch); vb.type = pick_type(rng);
                lodes.push_back(std::move(vb));
            }
        }
        VeinPolyline v; v.points = std::move(line); v.type = pick_type(rng);
        lodes.push_back(std::move(v));
    }

    // ----- 2) Paint lode densities via distance-to-polyline -----
    const float halfT = std::max(0.3f, P.lode_half_thickness);
    const float pwr   = std::max(0.8f, P.lode_falloff_power);

    auto grade = [&](int x,int y, uint32_t s){
        float n = detail::fbm2(x*0.045f, y*0.045f, 4, 2.0f, 0.5f, s);
        return 0.5f + 0.5f * (n - 0.5f) * 2.0f; // ~0..1
    };

    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i = detail::idx(x,y,W);
        float best2 = std::numeric_limits<float>::infinity();
        OreType bestType = IRON;

        for (const auto& v : lodes){
            float d2 = detail::dist_polyline((float)x,(float)y, v.points);
            if (d2 < best2){ best2 = d2; bestType = v.type; }
        }

        if (best2 < std::numeric_limits<float>::infinity()){
            float d = std::sqrt(best2);
            float core = std::max(0.0f, 1.0f - std::pow(d / halfT, pwr));

            float g = (bestType==GOLD)
                      ? grade(x,y, 0xA1B2C3u) * 0.9f + 0.1f
                      : grade(x,y, 0x00C0FFu);

            float slopeBoost = 1.0f + P.slope_bias  * out.slope01[i];
            float uplandBoost= 1.0f + P.upland_bias * detail::clamp01((height01[i]-P.sea_level)/(1.0f-P.sea_level));

            float val = core * g * slopeBoost * uplandBoost;

            if (is_water(i) && P.suppress_under_sea) val *= P.underwater_multiplier;

            switch (bestType){
                case IRON:   iron[i]   = std::max(iron[i],   val); break;
                case COPPER: copper[i] = std::max(copper[i], val); break;
                case GOLD:   gold[i]   = std::max(gold[i],   val*0.8f); break; // keep rare
                default: break;
            }
        }
    }

    // ----- 3) Stratiform coal seams -----
    detail::RNG crng(P.seed ^ 0x9E3779B97F4A7C15ULL);
    for (int s=0; s<P.seam_count; ++s){
        float base = detail::lerp(P.sea_level + 0.03f, 0.80f, crng.uf(0.0f, 0.4f)); // normalized altitude
        uint32_t foldSeed = (uint32_t)(P.seed + s*1337u);

        for (int y=0;y<H;++y) for (int x=0;x<W;++x){
            size_t i = detail::idx(x,y,W);
            float fx = x * P.seam_fold_scale, fy = y * P.seam_fold_scale;
            float fold = P.seam_fold_amp * ( detail::fbm2(fx,fy, 3, 2.0f, 0.5f, foldSeed) - 0.5f );

            float elev = height01[i];
            float seamElev = base + (fold / std::max(1.f,(float)H));
            float dist = std::abs(elev - seamElev);

            float within = std::max(0.0f, 1.0f - dist / (P.seam_thickness / std::max(1.f,(float)H)));
            if (within <= 0.0f) continue;

            float basin  = 1.0f - detail::clamp01((elev - P.sea_level)/(1.0f - P.sea_level));
            float gentle = 1.0f - out.slope01[i];
            float coal_here = within * (1.0f + P.seam_basin_bias*basin) * (1.0f + P.seam_slope_avoid*gentle);

            if (is_water(i) && P.suppress_under_sea) coal_here *= P.underwater_multiplier;
            coal[i] = std::max(coal[i], coal_here);
        }
    }

    // ----- 4) Quarry-grade stone (exposed rock) -----
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i = detail::idx(x,y,W);
        float elev = height01[i];
        float exposed = out.slope01[i];
        float upland  = detail::clamp01((elev - P.sea_level)/(1.0f-P.sea_level));
        float val = P.stone_from_slope*exposed + P.stone_from_height*upland;
        if (is_water(i) && P.suppress_under_sea) val *= P.underwater_multiplier;
        stone[i] = detail::clamp01(val);
    }

    out.lodes = std::move(lodes);
    return out;
}

/*
-------------------------------------- Usage --------------------------------------

#include "procgen/OreVeinGenerator.hpp"

void build_ores(const std::vector<float>& height01, int W, int H,
                const std::vector<uint8_t>* waterMask /*optional*/)
{
    procgen::OreParams P;
    P.width=W; P.height=H;
    P.regional_strike_deg = 30.0f;                 // align with mountain belt, if any
    P.fault_count = std::max(24, (W*H)/18000);     // scale by map size
    P.seam_count  = 3;                             // 2–4 works well

    procgen::OreResult R = procgen::GenerateOre(height01, W, H, P, waterMask);

    // Examples:
    //  • Sample densities to place ore nodes (thresholds or Poisson sampling).
    //  • Use R.lodes polylines to place mineshafts/adits along strike & lay trails.
    //  • Gate economy/tech by density (higher → richer veins).
    //  • Use COAL to spawn thick seam tiles; STONE to site quarries.
}
*/
} // namespace procgen
