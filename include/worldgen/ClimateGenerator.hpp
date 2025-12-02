#pragma once
// ============================================================================
// ClimateGenerator.hpp — header-only climate + biome generator for 2D grids
// C++17 / STL-only
//
// What it computes (from a heightmap):
//  • Temperature field (°C) using latitude gradient + adiabatic lapse-rate cooling
//  • Rainfall via a simple, scanline orographic precipitation model (rain shadow)
//  • Fertility index from temp/moisture
//  • Biome categories (Whittaker-style bands)
// ----------------------------------------------------------------------------
// Concept references (for the science; implementation here is original):
//  • Orographic precipitation & rain-shadow basics
//  • Temperature by latitude (insolation) & lapse rates (dry ~9.8°C/km; moist smaller)
//  • Whittaker biome classification (temperature × precipitation)
//
// Practical notes:
//  • Heights are expected normalized [0..1]. Provide meters_per_height_unit.
//  • Sea level used to mark ocean cells for moisture "recharge".
//  • Works in O(W*H) per wind pass (usually 2–3 passes are enough).
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

namespace worldgen {

struct ClimateParams {
    // --- Map & units ---
    float sea_level                = 0.50f;     // normalized [0..1]
    float meters_per_height_unit   = 1200.0f;   // scale from height units to meters

    // --- Latitude band of your map (used for base temperature) ---
    // Map row y=0 is 'north_lat_deg', y=H-1 is 'south_lat_deg'
    float north_lat_deg = 55.0f;
    float south_lat_deg = 15.0f;

    // --- Temperature model ---
    float t_equator_c   = 28.0f;  // °C at 0°
    float t_pole_c      = -18.0f; // °C at 90°
    float lapse_rate_c_per_km = 6.5f; // environmental ~6.5°C/km (dry is ~9.8°C/km)

    // --- Rain model (orographic / rain shadow) ---
    struct Wind {
        int dx = 1, dy = 0;       // cardinal step: (-1,0),(1,0),(0,-1),(0,1)
        float base_humidity = 1.0f; // starting moisture (recharged over ocean)
        float evap_recharge = 1.0f; // moisture value imposed over ocean cells
        float orographic_k  = 1.0f; // multiplier for upslope-induced rain
        float leak          = 0.04f;// fractional moisture lost per step (background rain)
        float leeward_dry   = 0.25f;// extra fractional loss on downslope (rain shadow)
    };
    // Default: mid-lat westerlies (W→E) and monsoonal S→N
    std::vector<Wind> winds = { {+1,0, 1.0f,1.0f, 1.2f,0.04f,0.25f},
                                {0,-1, 0.8f,1.0f, 0.9f,0.03f,0.20f} };

    // --- Output scaling ---
    float rainfall_scale_mm = 2500.0f; // multiply normalized rain to get mm/year
    // Fertility weighting (0..1)
    float temp_opt_c   = 18.0f;  // temp with max fertility
    float temp_width_c = 18.0f;  // bell curve width

    // Safety/robustness
    float epsilon = 1e-6f;
};

enum Biome : uint8_t {
    BIOME_OCEAN=0, BIOME_ICE, BIOME_TUNDRA, BIOME_BOREAL,
    BIOME_TEMPERATE_FOREST, BIOME_GRASSLAND, BIOME_SAVANNA,
    BIOME_DESERT, BIOME_TROPICAL_RAINFOREST
};

struct ClimateResult {
    int width=0, height=0;

    std::vector<float> temperature_c; // size W*H
    std::vector<float> rainfall_mm;   // size W*H
    std::vector<float> fertility01;   // size W*H (0..1)
    std::vector<uint8_t> biome;       // size W*H (Biome enum values)
};

// ------------------------ internals ------------------------

namespace detail {
    inline size_t idx(int x,int y,int W){ return (size_t)y*(size_t)W+(size_t)x; }
    inline bool inb(int x,int y,int W,int H){ return x>=0 && y>=0 && x<W && y<H; }

    inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
    inline float mix(float a,float b,float t){ return a + (b-a)*t; }

    // Cosine latitude temperature: T_lat = T_pole + (T_eq - T_pole) * cos(lat)
    inline float base_temp_from_lat(float lat_deg, float t_eq, float t_pole) {
        float r = std::cos(lat_deg * 3.1415926535f / 180.0f);
        if (r < 0.f) r = 0.f; // clamp beyond 90°
        return t_pole + (t_eq - t_pole) * r;
    }

    // Gaussian fertility vs temperature around an optimum
    inline float fertility_temp_curve(float T, float Topt, float width){
        float z = (T - Topt) / (width*0.5f);
        return std::exp(-z*z);
    }

    // Scanline order iterator for cardinal wind
    struct Scan {
        int x0,x1,y0,y1, sx, sy; // inclusive endpoints with step
    };
    inline Scan make_scan(int W,int H,int dx,int dy) {
        Scan s{};
        if (dx>0){ s.x0=0; s.x1=W-1; s.sx=+1; } else if (dx<0){ s.x0=W-1; s.x1=0; s.sx=-1; } else { s.x0=0; s.x1=W-1; s.sx=+1; }
        if (dy>0){ s.y0=0; s.y1=H-1; s.sy=+1; } else if (dy<0){ s.y0=H-1; s.y1=0; s.sy=-1; } else { s.y0=0; s.y1=H-1; s.sy=+1; }
        return s;
    }

    // Classify by a coarse Whittaker-like scheme on mean annual T and P
    inline Biome classify(float T, float Pmm, bool is_ocean) {
        if (is_ocean) return BIOME_OCEAN;
        if (T <= -8.0f) return BIOME_ICE;
        if (T <= 0.5f)  return (Pmm < 200.0f) ? BIOME_ICE : BIOME_TUNDRA;
        if (T <= 6.0f)  return (Pmm < 300.0f) ? BIOME_TUNDRA : BIOME_BOREAL;
        if (T <= 18.0f) {
            if (Pmm < 250.0f) return BIOME_DESERT;
            if (Pmm < 600.0f) return BIOME_GRASSLAND;
            return BIOME_TEMPERATE_FOREST;
        }
        // warm
        if (Pmm < 250.0f) return BIOME_DESERT;
        if (Pmm < 700.0f) return BIOME_SAVANNA;
        return BIOME_TROPICAL_RAINFOREST;
    }
}

// ------------------------ main API ------------------------

inline ClimateResult GenerateClimate(const std::vector<float>& height01, int W, int H,
                                     const ClimateParams& P,
                                     const std::vector<uint8_t>* water_mask /*optional, 1 = water*/)
{
    ClimateResult out; out.width=W; out.height=H;
    const size_t N = (size_t)W*(size_t)H;
    out.temperature_c.assign(N, 0.f);
    out.rainfall_mm.assign(N, 0.f);
    out.fertility01.assign(N, 0.f);
    out.biome.assign(N, BIOME_OCEAN);

    if (W<=0 || H<=0 || height01.size()!=N) return out;

    auto is_water = [&](size_t i)->bool {
        if (water_mask) return (*water_mask)[i] != 0;
        return height01[i] <= P.sea_level;
    };

    // --- 1) Temperature field ---
    for (int y=0; y<H; ++y) {
        float lat = detail::mix(P.north_lat_deg, P.south_lat_deg, (float)y / (float)(H-1));
        float Tlat = detail::base_temp_from_lat(lat, P.t_equator_c, P.t_pole_c); // °C

        for (int x=0; x<W; ++x) {
            size_t i = detail::idx(x,y,W);
            float h = std::max(0.0f, height01[i] - P.sea_level);
            float elev_m = h * P.meters_per_height_unit;
            // environmental lapse ~6.5 °C/km by default
            float T = Tlat - P.lapse_rate_c_per_km * (elev_m * 0.001f);
            out.temperature_c[i] = T;
        }
    }

    // --- 2) Rainfall (multi-wind orographic model) ---
    std::vector<float> rain_acc(N, 0.f);

    for (const auto& WN : P.winds) {
        if (!((WN.dx==0) ^ (WN.dy==0))) continue; // cardinal only

        auto scan = detail::make_scan(W,H, WN.dx, WN.dy);
        // process lines orthogonal to wind
        if (WN.dx != 0) {
            for (int y=scan.y0; ; y += scan.sy) {
                float moisture = WN.base_humidity;
                float prev_h = 0.0f;
                bool first = true;
                for (int x=scan.x0; ; x += scan.sx) {
                    size_t i = detail::idx(x,y,W);
                    float h = height01[i];
                    bool ocean = is_water(i);

                    if (ocean) moisture = std::max(moisture, WN.evap_recharge);

                    if (!first) {
                        float upslope = std::max(0.0f, (h - prev_h));
                        float downslope = std::max(0.0f, (prev_h - h));
                        // Orographic rain from upslope forcing + background ‘leak’
                        float precip = WN.orographic_k * upslope * moisture + WN.leak * moisture;
                        precip = std::min(precip, moisture); // cannot rain more than we have
                        rain_acc[i] += precip;
                        moisture -= precip;
                        // Leeward drying
                        moisture = std::max(0.0f, moisture * (1.0f - WN.leeward_dry * downslope));
                    } else {
                        first = false;
                    }
                    prev_h = h;
                    if (x == scan.x1) break;
                }
                if (y == scan.y1) break;
            }
        } else { // dy != 0
            for (int x=scan.x0; ; x += scan.sx) {
                float moisture = WN.base_humidity;
                float prev_h = 0.0f;
                bool first = true;
                for (int y=scan.y0; ; y += scan.sy) {
                    size_t i = detail::idx(x,y,W);
                    float h = height01[i];
                    bool ocean = is_water(i);

                    if (ocean) moisture = std::max(moisture, WN.evap_recharge);

                    if (!first) {
                        float upslope = std::max(0.0f, (h - prev_h));
                        float downslope = std::max(0.0f, (prev_h - h));
                        float precip = WN.orographic_k * upslope * moisture + WN.leak * moisture;
                        precip = std::min(precip, moisture);
                        rain_acc[i] += precip;
                        moisture -= precip;
                        moisture = std::max(0.0f, moisture * (1.0f - WN.leeward_dry * downslope));
                    } else {
                        first = false;
                    }
                    prev_h = h;
                    if (y == scan.y1) break;
                }
                if (x == scan.x1) break;
            }
        }
    }

    // Smooth-ish normalization: scale to mm/year
    float max_r = *std::max_element(rain_acc.begin(), rain_acc.end());
    float inv = (max_r > P.epsilon) ? (P.rainfall_scale_mm / max_r) : 0.0f;
    for (size_t i=0;i<N;++i) out.rainfall_mm[i] = rain_acc[i] * inv;

    // --- 3) Fertility & biome ---
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        size_t i = detail::idx(x,y,W);
        bool ocean = is_water(i);
        float T = out.temperature_c[i];
        float Pmm = out.rainfall_mm[i];

        // Fertility: product of temp curve and moisture curve (logistic)
        float temp_factor = detail::fertility_temp_curve(T, P.temp_opt_c, P.temp_width_c);
        float moist_factor = 1.0f - std::exp(-Pmm / 300.0f); // saturates ~exponentially
        out.fertility01[i] = detail::clamp01(temp_factor * moist_factor);

        out.biome[i] = detail::classify(T, Pmm, ocean);
    }

    return out;
}

/*
------------------------------------ Usage ------------------------------------
#include "worldgen/ClimateGenerator.hpp"

void build_climate_layers(const std::vector<float>& height01, int W, int H,
                          const std::vector<uint8_t>* waterMask /*optional*/)
{
    worldgen::ClimateParams P;
    // Good starting point if your map spans mid-latitudes in the N hemisphere:
    P.north_lat_deg = 55.0f; P.south_lat_deg = 15.0f;
    P.sea_level = 0.50f; P.meters_per_height_unit = 1200.0f;
    // Two winds: Westerlies (W→E) and a S→N monsoonal component:
    P.winds = { {+1,0,1.0f,1.0f,1.2f,0.04f,0.25f}, {0,-1,0.8f,1.0f,0.9f,0.03f,0.20f} };

    worldgen::ClimateResult C = worldgen::GenerateClimate(height01, W, H, P, waterMask);

    // Use layers:
    //  • C.temperature_c: color LUT for climate map; decrease crop growth when < 5°C
    //  • C.rainfall_mm: drive forest density, wetland placement, and lake persistence
    //  • C.fertility01: modulate farmland yield & wild plant spawn rates
    //  • C.biome: pick tilesets/foliage tables/resources by biome
}
*/
} // namespace worldgen
