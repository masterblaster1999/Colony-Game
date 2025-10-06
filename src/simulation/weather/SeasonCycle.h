#pragma once
// src/simulation/weather/SeasonCycle.h
// Minimal, deterministic season + daylight + baseline climate helpers.
// No dependencies beyond the C++ standard library.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace colony::weather {

constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
inline double wrap01(double x) {
    double y = std::fmod(x, 1.0);
    return (y < 0.0) ? (y + 1.0) : y;
}
inline double deg2rad(double d) { return d * (kPi / 180.0); }

enum class Season { Winter, Spring, Summer, Autumn };

struct SeasonConfig {
    // World/astronomy-ish knobs (tune to taste)
    double year_length_days        = 360.0;  // In‑game year length (days)
    double axial_tilt_deg          = 23.5;   // Earth-like by default
    double phase_north             = 0.0;    // 0.0 means Winter starts at day 0 in N. hemisphere

    // Thermal / humidity baselines (temperate default)
    double base_temp_c             = 10.0;   // Sea-level baseline annual mean
    double seasonal_temp_amp_c     = 12.0;   // Summer/Winter swing
    double diurnal_temp_amp_c      = 6.0;    // Day/night swing (modulated by daylight length)
    double lapse_rate_c_per_km     = 6.5;    // Standard atmosphere-ish

    double humidity_base           = 0.55;   // Annual mean relative humidity
    double humidity_seasonal_amp   = 0.15;   // +/- swing across the year

    // “Wet season” placement (for humidity/precip bias)
    double precip_wet_season_center = 0.25;  // 0..1, 0.25 ~ spring in N. hemisphere
};

struct SeasonState {
    Season season;
    double day_of_year01;   // 0..1
    double daylight_hours;  // 0..24
};

// Normalized day-of-year in [0,1)
inline double dayOfYear01(double time_days, double year_length_days) {
    return wrap01(time_days / year_length_days);
}

// Approximate astronomical day length (hours) for given latitude and day-of-year.
// Declination δ ≈ tilt * sin(2π * doy01). cos(H0) = -tanφ * tanδ.
inline double daylightHours(double latitude_deg, double doy01, double axial_tilt_deg = 23.5) {
    const double lat = deg2rad(latitude_deg);
    const double tilt = deg2rad(axial_tilt_deg);
    const double dec = std::asin(std::sin(tilt) * std::sin(2.0 * kPi * doy01));
    const double x = -std::tan(lat) * std::tan(dec);
    if (x >= 1.0)  return 0.0;   // Polar night
    if (x <= -1.0) return 24.0;  // Midnight sun
    const double H0 = std::acos(x); // radians
    return (24.0 / kPi) * H0;
}

// Which season bucket we’re in (4 equal arcs with hemisphere-aware phase).
inline Season seasonAt(double latitude_deg, double doy01, const SeasonConfig& cfg) {
    const double hemi_shift = (latitude_deg < 0.0) ? 0.5 : 0.0; // flip seasons in S. hemisphere
    const double s = wrap01(doy01 + hemi_shift - cfg.phase_north);
    if (s < 0.25) return Season::Winter;
    if (s < 0.50) return Season::Spring;
    if (s < 0.75) return Season::Summer;
    return Season::Autumn;
}

// Baseline annual temperature at sea level (no diurnal, no altitude).
inline double seasonalTempC(double latitude_deg, double doy01, const SeasonConfig& cfg) {
    const double hemi_shift = (latitude_deg < 0.0) ? 0.5 : 0.0;
    const double s = wrap01(doy01 + hemi_shift - cfg.phase_north);
    // Peaks at mid‑summer (s ~ 0.5), troughs at mid‑winter (s ~ 0.0)
    const double annual_wave = std::cos(2.0 * kPi * (s - 0.5)); // [-1, 1]
    return cfg.base_temp_c + cfg.seasonal_temp_amp_c * annual_wave;
}

// Diurnal (day/night) temperature offset based on local time and day length.
inline double diurnalTempOffsetC(double local_time01, double daylight_hours, const SeasonConfig& cfg) {
    // Scale diurnal amplitude by how long the day is (longer day → stronger swing).
    const double swing_scale = clamp01(daylight_hours / 12.0); // ~0..2 → clamp 0..1
    const double amp = cfg.diurnal_temp_amp_c * (0.3 + 0.7 * swing_scale);
    // Midday at local_time01 = 0.5 → warmest; midnight (0.0/1.0) → coolest.
    const double diurnal_wave = std::cos(2.0 * kPi * (local_time01 - 0.5)); // [-1, 1]
    return amp * diurnal_wave;
}

// Humidity baseline with a hemispheric wet-season bias.
inline double humidityBaseline(double latitude_deg, double doy01, const SeasonConfig& cfg) {
    const double hemi_shift = (latitude_deg < 0.0) ? 0.5 : 0.0;
    const double s = wrap01(doy01 + hemi_shift - cfg.phase_north);
    // Peak humidity at wet_season_center (cosine envelope)
    const double wet_wave = std::cos(2.0 * kPi * wrap01(s - cfg.precip_wet_season_center)); // [-1,1]
    return clamp01(cfg.humidity_base + cfg.humidity_seasonal_amp * wet_wave);
}

// Convenience: compute all season state from world/time.
inline SeasonState computeSeasonState(double time_days, double latitude_deg, const SeasonConfig& cfg) {
    const double doy = dayOfYear01(time_days, cfg.year_length_days);
    return SeasonState{
        seasonAt(latitude_deg, doy, cfg),
        doy,
        daylightHours(latitude_deg, doy, cfg.axial_tilt_deg)
    };
}

} // namespace colony::weather
