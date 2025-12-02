#pragma once
// ============================================================================
// ClimateGenerator.hpp — header-only climate, seasons, and biomes for grids
// C++17 / STL-only. No external dependencies.
//
// Outputs per cell (W×H):
//  • mean_temp_C, mean_rain_mm
//  • monthly_temp_C[12], monthly_rain_mm[12]
//  • gdd_base10 (growing degree days, base 10 °C)
//  • biome_id (enumeration)
//  • debug: slope01 (normalized), gradX/gradY (unitless)
//
// Concepts used (implementation here is original):
//  • Orographic precipitation & rain shadows (NOAA/UBC).
//  • Temperature lapse with elevation (~6.5 °C/km standard atmosphere).
//  • Seasonal cycle via solar declination (±23.44°) with hemisphere phase.
//  • Prevailing wind defaults by latitude (trades / westerlies / polar easterlies).
//  • Biome classification using a simplified Whittaker-style scheme.
// ============================================================================

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

namespace worldgen {

struct ClimateParams {
    // Grid & terrain interpretation
    int   width = 0, height = 0;
    float sea_level = 0.50f;                 // height01 <= sea_level => water
    float elevation_range_m = 3000.0f;       // meters represented from sea_level..1.0
    float lapse_rate_c_per_km = 6.5f;        // °C/km (standard atmosphere)

    // Location & seasons
    float latitude_deg = 40.0f;              // −90..+90; affects seasonal amplitude
    bool  north_hemisphere = true;           // seasonal phase (true=NH, false=SH)

    // Rain model
    float base_annual_rain_mm = 900.0f;      // crude climatology baseline
    float orographic_up_gain   = 0.6f;       // ↑ rain per unit upslope (dimensionless)
    float lee_dry_gain         = 0.4f;       // rain reduction per unit lee slope
    float lee_decay            = 0.85f;      // IIR decay (0..1) for downwind dry "shadow"
    int   shadow_passes        = 1;          // repeat IIR once or twice for longer shadows

    // Winds (cardinal directions, weight sum ≈ 1). If empty, inferred from latitude.
    struct Wind { int dx, dy; float weight; }; // dx,dy ∈ {−1,0,1}, not both zero
    std::vector<Wind> winds;
};

enum Biome : uint8_t {
    OCEAN=0, ICE=1, TUNDRA=2, BOREAL_FOREST=3, TEMPERATE_GRASSLAND=4, TEMPERATE_FOREST=5,
    MEDITERRANEAN_SHRUB=6, DESERT=7, SAVANNA=8, TROPICAL_RAINFOREST=9, WETLAND=10
};

struct ClimateResult {
    int width=0, height=0;
    std::vector<float> mean_temp_C;                 // size W*H
    std::vector<float> mean_rain_mm;                // size W*H
    std::array<std::vector<float>,12> monthly_temp_C; // each size W*H
    std::array<std::vector<float>,12> monthly_rain_mm;// each size W*H
    std::vector<float> gdd_base10;                  // size W*H
    std::vector<uint8_t> biome_id;                  // size W*H

    // Debug
    std::vector<float> slope01, gradX, gradY;       // size W*H
};

namespace detail {

inline size_t I(int x,int y,int W){ return (size_t)y*(size_t)W + (size_t)x; }
inline bool inb(int x,int y,int W,int H){ return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H; }
inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

// Central-difference gradient (unitless, on normalized height)
inline void gradient(const std::vector<float>& h, int W, int H,
                     std::vector<float>& gx, std::vector<float>& gy, std::vector<float>& slope01)
{
    gx.assign((size_t)W*H, 0.f);
    gy.assign((size_t)W*H, 0.f);
    slope01.assign((size_t)W*H, 0.f);

    auto Hs=[&](int x,int y){
        x=std::clamp(x,0,W-1); y=std::clamp(y,0,H-1);
        return h[I(x,y,W)];
    };
    float gmax=1e-6f;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        float Gx = 0.5f * (Hs(x+1,y) - Hs(x-1,y));
        float Gy = 0.5f * (Hs(x,y+1) - Hs(x,y-1));
        gx[I(x,y,W)] = Gx; gy[I(x,y,W)] = Gy;
        float mag = std::sqrt(Gx*Gx + Gy*Gy);
        slope01[I(x,y,W)] = mag; gmax = std::max(gmax, mag);
    }
    for(float& v : slope01) v /= gmax; // normalize for debug/consistency
}

// Directional IIR along cardinal winds to smear leeward drying (simple "shadow")
inline void iir_shadow(std::vector<float>& field, int W,int H, int dx,int dy, float decay, int passes)
{
    if (dx==0 && dy==0) return;
    for (int p=0;p<passes;++p){
        if (dx!=0){ // sweep rows
            for (int y=0;y<H;++y){
                float acc=0.f;
                if (dx>0){ // west -> east
                    for (int x=0;x<W;++x){ size_t i=I(x,y,W); acc = acc*decay + field[i]; field[i]=acc; }
                }else{     // east -> west
                    for (int x=W-1;x>=0;--x){ size_t i=I(x,y,W); acc = acc*decay + field[i]; field[i]=acc; }
                }
            }
        } else {     // sweep columns
            for (int x=0;x<W;++x){
                float acc=0.f;
                if (dy>0){ // north -> south
                    for (int y=0;y<H;++y){ size_t i=I(x,y,W); acc = acc*decay + field[i]; field[i]=acc; }
                }else{     // south -> north
                    for (int y=H-1;y>=0;--y){ size_t i=I(x,y,W); acc = acc*decay + field[i]; field[i]=acc; }
                }
            }
        }
    }
}

// Build default wind set from latitude: trades/easterlies vs westerlies
inline std::vector<ClimateParams::Wind> default_winds(float lat_deg){
    float alat = std::fabs(lat_deg);
    if (alat < 30.f) { // tropics → trades (easterlies, blow E->W: dx=-1)
        return { { -1, 0, 0.7f }, { 0, 1, 0.15f }, { 0, -1, 0.15f } };
    } else if (alat < 60.f) { // mid-lats → westerlies (W->E: dx=+1)
        return { { +1, 0, 0.7f }, { 0, 1, 0.15f }, { 0, -1, 0.15f } };
    } else { // polar → easterlies again
        return { { -1, 0, 0.7f }, { 0, 1, 0.15f }, { 0, -1, 0.15f } };
    }
}

// Seasonal temperature curve (monthly) based on latitude and hemisphere
inline float monthly_temp_at(bool northHem, int month0to11, float meanLatC, float ampC){
    int phase = northHem ? 6 : 0;  // warmest ~July in NH, ~January in SH
    float theta = 2.0f * 3.14159265358979323846f * (float)(month0to11 - phase) / 12.0f;
    return meanLatC + ampC * std::cos(theta);
}

// Simple Whittaker-like biome classification from MAT/MAP (+ ice/wetland guards)
inline uint8_t classify_biome(float matC, float mapMM, float minMonthC, bool isWater){
    if (isWater) return OCEAN;
    if (minMonthC < -5.0f && mapMM < 400.0f) return ICE; // crude perma-ice guard
    if (mapMM < 250.0f) return DESERT;                   // textbook desert cutoff
    if (matC < 0.0f) return TUNDRA;
    if (matC < 5.0f) return BOREAL_FOREST;
    if (matC > 24.0f && mapMM > 2000.0f) return TROPICAL_RAINFOREST;
    if (matC > 20.0f && mapMM >= 500.0f && mapMM <= 1500.0f) return SAVANNA;
    if (matC >= 5.0f && matC <= 17.0f && mapMM >= 700.0f) return TEMPERATE_FOREST;
    if (matC >= 10.0f && mapMM >= 400.0f && mapMM < 700.0f) return TEMPERATE_GRASSLAND;
    // dry‑summer mid‑latitudes (very rough stand‑in)
    if (matC >= 10.0f && mapMM >= 300.0f && mapMM < 700.0f) return MEDITERRANEAN_SHRUB;
    return TEMPERATE_GRASSLAND;
}

} // namespace detail

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
inline ClimateResult GenerateClimate(const std::vector<float>& height01,
                                     int W, int H,
                                     const ClimateParams& P = {},
                                     const std::vector<uint8_t>* waterMask /*optional, 1=water*/)
{
    ClimateResult out; out.width=W; out.height=H;
    const size_t N=(size_t)W*H;
    if (W<=1 || H<=1 || height01.size()!=N) return out;

    // 1) Terrain primitives
    detail::gradient(height01, W, H, out.gradX, out.gradY, out.slope01);

    // 2) Winds
    std::vector<ClimateParams::Wind> winds = P.winds.empty() ? detail::default_winds(P.latitude_deg)
                                                             : P.winds;

    // 3) Orographic precipitation proxy
    std::vector<float> rain_orographic(N, 0.f);
    std::vector<float> lee(N, 0.f), lee_tmp;

    for (const auto& w : winds){
        if ((w.dx==0 && w.dy==0) || w.weight<=0.f) continue;

        // Local upslope / lee from gradient projection along wind
        for (int y=0;y<H;++y) for (int x=0;x<W;++x){
            size_t i=detail::I(x,y,W);
            float proj = out.gradX[i]*w.dx + out.gradY[i]*w.dy; // d(height)/d(along-wind)
            float up  = std::max(0.0f, proj);
            float dn  = std::max(0.0f, -proj);
            rain_orographic[i] += w.weight * P.orographic_up_gain * up;
            lee[i]             += w.weight * dn;
        }

        // Spread lee dryness downwind as an exponential IIR along this wind
        lee_tmp = lee; // copy current lee component
        detail::iir_shadow(lee_tmp, W, H, w.dx, w.dy, P.lee_decay, std::max(1, P.shadow_passes));
        for (size_t i=0;i<N;++i) rain_orographic[i] -= P.lee_dry_gain * lee_tmp[i];
        std::fill(lee.begin(), lee.end(), 0.0f); // reset accumulator for next wind
    }

    // Normalize/shift to keep rain positive and stable
    float rmin=std::numeric_limits<float>::infinity(), rmax=-rmin;
    for(float v: rain_orographic){ rmin=std::min(rmin,v); rmax=std::max(rmax,v); }
    float range = std::max(1e-6f, rmax - rmin);
    for(float& v: rain_orographic) v = (v - rmin) / range; // 0..1

    // Base annual rain per cell (mm)
    out.mean_rain_mm.assign(N, 0.f);
    for (size_t i=0;i<N;++i){
        bool water = waterMask ? ((*waterMask)[i]!=0) : (height01[i] <= P.sea_level);
        float base = P.base_annual_rain_mm;
        // modest coastal boost near sea level
        if (!water) base *= 1.0f + 0.15f * (1.0f - std::fabs(height01[i]-P.sea_level)*2.0f);
        out.mean_rain_mm[i] = std::max(50.0f, base * (0.6f + 0.8f*rain_orographic[i]));
    }

    // 4) Temperature: latitude + seasons + lapse with elevation
    // Mean by latitude (rough): ~27°C at equator, ~−15°C at pole
    float alat = std::fabs(P.latitude_deg) * 3.14159265358979323846f / 180.0f;
    float meanLatC = -15.0f + 42.0f * std::cos(alat);
    float ampC     =  2.0f + 18.0f * std::pow(std::sin(alat), 0.8f); // stronger seasons at higher lat

    for (int m=0;m<12;++m){ out.monthly_temp_C[m].assign(N, 0.f); out.monthly_rain_mm[m].assign(N, 0.f); }

    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        size_t i=detail::I(x,y,W);
        bool water = waterMask ? ((*waterMask)[i]!=0) : (height01[i] <= P.sea_level);

        float elev_m = std::max(0.0f, height01[i] - P.sea_level) * P.elevation_range_m;
        float elev_km = elev_m * 0.001f;

        float t_sum=0.f, r_sum=0.f, t_min=1e9f;
        float rainAmp = 0.25f + 0.25f * std::pow(std::sin(alat), 0.7f);
        int phase = P.north_hemisphere ? 6 : 0;

        for (int m=0;m<12;++m){
            float t_m = detail::monthly_temp_at(P.north_hemisphere, m, meanLatC, ampC);
            t_m -= P.lapse_rate_c_per_km * elev_km; // lapse with altitude
            if (water) t_m = std::max(t_m, -1.0f);  // simple clamp for seas

            float s = 1.0f + rainAmp * std::cos(2.0f*3.14159265358979323846f * (float)(m - phase) / 12.0f);
            float r_m = out.mean_rain_mm[i] * s / 12.0f; // distribute annual into months

            out.monthly_temp_C[m][i] = t_m;
            out.monthly_rain_mm[m][i] = std::max(0.0f, r_m);

            t_sum += t_m;
            r_sum += r_m;
            t_min = std::min(t_min, t_m);
        }

        out.mean_temp_C.push_back(t_sum / 12.0f);
        out.mean_rain_mm[i] = std::max(0.0f, r_sum); // keep annual consistent

        // Growing degree days, base 10°C (rough monthly*30 approximation)
        float gdd=0.f;
        for (int m=0;m<12;++m){
            gdd += std::max(0.0f, out.monthly_temp_C[m][i] - 10.0f) * 30.0f;
        }
        out.gdd_base10.push_back(gdd);

        // Biome classification (approx), with wetland guard on flat, wet areas
        float flatWet = (out.slope01[i] < 0.05f && out.mean_rain_mm[i] > 1500.0f) ? 1.0f : 0.0f;
        uint8_t b = detail::classify_biome(out.mean_temp_C[i], out.mean_rain_mm[i], t_min, water);
        if (!water && flatWet) b = WETLAND;
        out.biome_id.push_back(b);
    }

    return out;
}

/*
------------------------------------- Usage -------------------------------------

#include "worldgen/ClimateGenerator.hpp"

void build_climate(const std::vector<float>& height01, int W, int H,
                   const std::vector<uint8_t>* waterMask /*optional*/)
{
    worldgen::ClimateParams P;
    P.width=W; P.height=H;
    P.sea_level = 0.50f;
    P.latitude_deg = 35.0f;       // set for your world
    P.north_hemisphere = true;
    // Optionally override default winds:
    // P.winds = { {+1,0,0.7f}, {0,1,0.15f}, {0,-1,0.15f} }; // westerlies + minor N/S

    worldgen::ClimateResult C = worldgen::GenerateClimate(height01, W, H, P, waterMask);

    // Hooks:
    //  • Spawn vegetation & wildlife from C.biome_id / C.mean_rain_mm.
    //  • Scale farm yields by C.gdd_base10 and summer rainfall.
    //  • Gate building chains (e.g., wells/irrigation) by dryness.
    //  • Drive seasonal visuals from C.monthly_temp_C / C.monthly_rain_mm.
}
*/
} // namespace worldgen
