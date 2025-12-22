#pragma once

// Climate parameters used by worldgen configuration (StagesConfig) and/or
// climate-related generation stages.
//
// Keep this header lightweight and platform-agnostic.

namespace cg {

struct ClimateParams final {
  // Temperature decrease with altitude (Kelvin per kilometer).
  // Typical tropospheric lapse rate ~ 6.5 K/km.
  float lapseRateKPerKm = 6.5f;

  // Temperature change per degree latitude away from equator (Kelvin per degree).
  // This is a simple "big picture" gradient used by the toy climate model.
  float latGradientKPerDeg = 0.6f;

  // Baseline sea level temperature (Kelvin). 288.15 K ≈ 15°C.
  float seaLevelTempK = 288.15f;
};

} // namespace cg
