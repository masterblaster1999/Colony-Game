#pragma once
// src/simulation/weather/WeatherSystem.h
// Deterministic procedural weather field sampling built on SeasonCycle.
// Produces temperature, humidity, wind, pressure, cloudiness, and precip.

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "SeasonCycle.h"

namespace colony::weather {

constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// --- Tiny hash & value noise -------------------------------------------------

// 64-bit SplitMix hash
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Deterministic pseudo-random [0,1) from integer lattice + seed
inline double prand01(int32_t x, int32_t y, uint64_t seed) {
    uint64_t h = splitmix64(static_cast<uint64_t>(x) * 0x9E3779B1u
                          ^ static_cast<uint64_t>(y) * 0x85EBCA77u
                          ^ seed);
    // Convert to double in [0,1)
    return (h >> 11) * (1.0 / 9007199254740992.0); // 53-bit / 2^53
}

inline double prand01(int32_t x, int32_t y, int32_t z, uint64_t seed) {
    uint64_t h = splitmix64(static_cast<uint64_t>(x) * 0x9E3779B1u
                          ^ static_cast<uint64_t>(y) * 0x85EBCA77u
                          ^ static_cast<uint64_t>(z) * 0xC2B2AE3Du
                          ^ seed);
    return (h >> 11) * (1.0 / 9007199254740992.0);
}

inline double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

// 2D value noise with smooth interpolation (returns [0,1])
inline double valueNoise2D(double x, double y, uint64_t seed) {
    const int32_t xi = static_cast<int32_t>(std::floor(x));
    const int32_t yi = static_cast<int32_t>(std::floor(y));
    const double  xf = x - xi;
    const double  yf = y - yi;
    const double  s = prand01(xi,     yi,     seed);
    const double  t = prand01(xi + 1, yi,     seed);
    const double  u = prand01(xi,     yi + 1, seed);
    const double  v = prand01(xi + 1, yi + 1, seed);
    const double  sx = smoothstep(xf);
    const double  sy = smoothstep(yf);
    const double  a = lerp(s, t, sx);
    const double  b = lerp(u, v, sx);
    return lerp(a, b, sy);
}

// 3D value noise with smooth interpolation (returns [0,1])
inline double valueNoise3D(double x, double y, double z, uint64_t seed) {
    const int32_t xi = static_cast<int32_t>(std::floor(x));
    const int32_t yi = static_cast<int32_t>(std::floor(y));
    const int32_t zi = static_cast<int32_t>(std::floor(z));
    const double  xf = x - xi, yf = y - yi, zf = z - zi;
    const double  sx = smoothstep(xf), sy = smoothstep(yf), sz = smoothstep(zf);

    auto n = [&](int dx, int dy, int dz){ return prand01(xi+dx, yi+dy, zi+dz, seed); };

    // Trilinear interpolation of 8 corners
    const double c000 = n(0,0,0), c100 = n(1,0,0), c010 = n(0,1,0), c110 = n(1,1,0);
    const double c001 = n(0,0,1), c101 = n(1,0,1), c011 = n(0,1,1), c111 = n(1,1,1);
    const double x00 = lerp(c000, c100, sx), x10 = lerp(c010, c110, sx);
    const double x01 = lerp(c001, c101, sx), x11 = lerp(c011, c111, sx);
    const double y0  = lerp(x00,  x10,  sy), y1  = lerp(x01,  x11,  sy);
    return lerp(y0, y1, sz);
}

inline double fbm2(double x, double y, int oct, double lac, double gain, uint64_t seed) {
    double amp = 1.0, freq = 1.0, sum = 0.0, norm = 0.0;
    for (int i = 0; i < oct; ++i) {
        sum  += amp * valueNoise2D(x * freq, y * freq, seed + static_cast<uint64_t>(i));
        norm += amp;
        amp  *= gain;
        freq *= lac;
    }
    return (norm > 0.0) ? (sum / norm) : 0.0; // [0,1]
}

inline double fbm3(double x, double y, double z, int oct, double lac, double gain, uint64_t seed) {
    double amp = 1.0, freq = 1.0, sum = 0.0, norm = 0.0;
    for (int i = 0; i < oct; ++i) {
        sum  += amp * valueNoise3D(x * freq, y * freq, z * freq, seed + static_cast<uint64_t>(i));
        norm += amp;
        amp  *= gain;
        freq *= lac;
    }
    return (norm > 0.0) ? (sum / norm) : 0.0; // [0,1]
}

// --- Weather model -----------------------------------------------------------

struct WeatherSystemConfig {
    uint64_t seed                        = 0xC01_0NY_uLL; // world/global seed

    // Spatial scaling: world units → "weather UV" (larger denom → larger features)
    double field_scale_large             = 1.0 / 20000.0; // ~20 km features
    double field_scale_small             = 1.0 / 4000.0;  // ~4 km features

    // Temporal evolution (cycles per in‑game day)
    double storm_speed_cpd               = 0.05;  // fronts move slowly
    double windfield_speed_cpd           = 0.08;

    // Intensities & thresholds
    double precip_intensity_max_mmph     = 8.0;   // cap for rain/snow (mm per hour)
    double storm_threshold               = 0.62;  // how “strong” the storm field must be to precipitate
    double cloudiness_base               = 0.25;  // baseline cloud cover
    double cloudiness_from_humidity      = 0.60;  // how strongly humidity raises cloudiness
    double cloudiness_from_storm         = 0.35;  // how strongly storms raise cloudiness

    double wind_speed_max_mps            = 18.0;  // Beaufort 8-ish
    double pressure_base_hpa             = 1013.0;
    double pressure_variation_hpa        = 16.0;

    // Optional: additional terrain coupling (simple altitude dampers)
    double precip_altitude_damp_start_m  = 1200.0; // start reducing precip above this
    double precip_altitude_damp_per_km   = 0.35;   // per km above start
};

struct WeatherSample {
    double temperature_c         = 0.0;
    double humidity              = 0.0;  // 0..1
    double cloudiness            = 0.0;  // 0..1
    double precipitation_mmph    = 0.0;  // rain or snow water-equivalent
    double rainfall_mmph         = 0.0;  // liquid
    double snowfall_mmph         = 0.0;  // water-equivalent (you can convert to cm using snow density)
    bool   is_snow               = false;

    double wind_speed_mps        = 0.0;
    double wind_dir_rad          = 0.0;  // 0 = east, π/2 = north
    double pressure_hpa          = 1013.0;

    bool   is_storm              = false; // heavy precip + notable wind
};

// Utility: simple altitude-based precipitation damping
inline double altitudeDamp(double altitude_m, double start_m, double per_km) {
    if (altitude_m <= start_m) return 1.0;
    const double km_over = (altitude_m - start_m) / 1000.0;
    return std::max(0.0, 1.0 - per_km * km_over);
}

class WeatherSystem {
public:
    WeatherSystem(WeatherSystemConfig wcfg = {}, SeasonConfig scfg = {})
        : wcfg_(wcfg), scfg_(scfg) {}

    const WeatherSystemConfig& config() const { return wcfg_; }
    const SeasonConfig& seasonConfig()  const { return scfg_; }

    // Sample the weather field at (x,y) world units, altitude (m), latitude (deg),
    // and absolute game time (days). local_time01 is local time-of-day in [0,1).
    WeatherSample sample(double time_days,
                         double local_time01,
                         double x, double y,
                         double altitude_m,
                         double latitude_deg) const
    {
        // --- Seasonal baselines
        const auto seasonState = computeSeasonState(time_days, latitude_deg, scfg_);
        const double base_temp_seasonal = seasonalTempC(latitude_deg, seasonState.day_of_year01, scfg_);
        const double diurnal_offset     = diurnalTempOffsetC(local_time01, seasonState.daylight_hours, scfg_);
        const double temp_alt_penalty   = (altitude_m / 1000.0) * scfg_.lapse_rate_c_per_km;
        const double temperature_c      = base_temp_seasonal + diurnal_offset - temp_alt_penalty;

        double humidity = humidityBaseline(latitude_deg, seasonState.day_of_year01, scfg_);

        // --- Noise fields (deterministic)
        // Spatial UVs (large & small structures)
        const double uL = (x + 1e-3) * wcfg_.field_scale_large;
        const double vL = (y - 1e-3) * wcfg_.field_scale_large;
        const double uS = x * wcfg_.field_scale_small;
        const double vS = y * wcfg_.field_scale_small;

        // Temporal phase
        const double tStorm = time_days * wcfg_.storm_speed_cpd;
        const double tWind  = time_days * wcfg_.windfield_speed_cpd;

        // Storm field: slow-moving, multi-octave FBM
        const double stormL = fbm3(uL, vL, tStorm, 4, 2.0, 0.5, wcfg_.seed + 0x1111);
        const double stormS = fbm3(uS, vS, tStorm * 1.7, 3, 2.1, 0.55, wcfg_.seed + 0x2222);
        const double storm_field01 = clamp01(0.6 * stormL + 0.4 * stormS);  // [0,1]

        // Wind field: direction from phase noise, speed from magnitude noise
        const double wind_dir01 = fbm2(uL * 0.7 + 17.0, vL * 0.7 - 11.0, 3, 2.2, 0.5, wcfg_.seed + 0x3333);
        const double wind_spd01 = std::pow(fbm2(uL * 0.9 + tWind, vL * 0.9 - tWind, 4, 2.0, 0.5, wcfg_.seed + 0x4444), 1.35);
        const double wind_dir   = 2.0 * kPi * wind_dir01; // radians
        double wind_speed_mps   = 0.15 * wcfg_.wind_speed_max_mps + (wcfg_.wind_speed_max_mps * 0.85) * wind_spd01;

        // Pressure anticorrelates with storms
        const double pressure = wcfg_.pressure_base_hpa
                              + wcfg_.pressure_variation_hpa * (0.5 - storm_field01);

        // Cloudiness from humidity + storms + small texture
        const double cloud_tex = fbm2(uS * 1.3 + 5.0, vS * 1.3 - 3.0, 3, 2.2, 0.5, wcfg_.seed + 0x5555);
        double cloudiness = wcfg_.cloudiness_base
                          + wcfg_.cloudiness_from_humidity * (humidity - 0.5) * 0.9
                          + wcfg_.cloudiness_from_storm    * (storm_field01 - 0.5) * 1.1
                          + (cloud_tex - 0.5) * 0.2;
        cloudiness = clamp01(cloudiness);

        // Humidity small-scale variation
        humidity = clamp01(humidity + (cloud_tex - 0.5) * 0.15);

        // Precip: only if storm field clears threshold; scale by humidity and cloudiness
        double precip01 = 0.0;
        if (storm_field01 > wcfg_.storm_threshold) {
            const double excess = (storm_field01 - wcfg_.storm_threshold) / (1.0 - wcfg_.storm_threshold);
            precip01 = clamp01(0.6 * excess + 0.3 * (humidity) + 0.1 * cloudiness);
        }

        // Altitude damping (rain shadow effect, crude but cheap)
        precip01 *= altitudeDamp(altitude_m, wcfg_.precip_altitude_damp_start_m, wcfg_.precip_altitude_damp_per_km);

        double precipitation_mmph = wcfg_.precip_intensity_max_mmph * precip01;

        // Phase: snow if near/below freezing (tune threshold margin)
        const bool is_snow = (temperature_c <= 0.5);
        double snowfall_mmph = is_snow ? precipitation_mmph : 0.0;
        double rainfall_mmph = is_snow ? 0.0 : precipitation_mmph;

        // “Storm” flag if it’s really coming down and windy
        const bool is_storm = (precipitation_mmph >= 3.0) && (wind_speed_mps >= 8.0);

        return WeatherSample{
            temperature_c,
            humidity,
            cloudiness,
            precipitation_mmph,
            rainfall_mmph,
            snowfall_mmph,
            is_snow,
            wind_speed_mps,
            wind_dir,
            pressure,
            is_storm
        };
    }

private:
    WeatherSystemConfig wcfg_;
    SeasonConfig        scfg_;
};

} // namespace colony::weather
