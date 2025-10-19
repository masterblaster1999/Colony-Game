// src/worldgen/Hydrology.hpp
#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstddef> // size_t

namespace cg {

// Lightweight 2D scalar field used throughout the hydrology pipeline.
// Values are typically height in "meters" or arbitrary scalar units.
struct HeightField {
    int w = 0;
    int h = 0;
    std::vector<float> data;

    HeightField() = default;
    HeightField(int width, int height, float init = 0.0f)
        : w(width), h(height), data(static_cast<size_t>(width) * height, init) {}

    void resize(int width, int height, float init = 0.0f) {
        w = width; h = height;
        data.assign(static_cast<size_t>(width) * height, init);
    }

    [[nodiscard]] size_t size() const noexcept { return data.size(); }

    [[nodiscard]] float*       raw()       noexcept { return data.data(); }
    [[nodiscard]] const float* raw() const noexcept { return data.data(); }

    // ---- small quality-of-life additions (non-breaking) ----
    [[nodiscard]] bool inBounds(int x, int y) const noexcept {
        return (x >= 0 && x < w && y >= 0 && y < h);
    }
    [[nodiscard]] size_t indexUnchecked(int x, int y) const noexcept {
        return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
    }

    [[nodiscard]] float& at(int x, int y) {
        assert(inBounds(x, y));
        return data[indexUnchecked(x, y)];
    }
    [[nodiscard]] const float& at(int x, int y) const {
        assert(inBounds(x, y));
        return data[indexUnchecked(x, y)];
    }

    // Convenience accessors; identical semantics to at()
    [[nodiscard]] float& operator()(int x, int y) { return at(x, y); }
    [[nodiscard]] const float& operator()(int x, int y) const { return at(x, y); }

    void fill(float v) { std::fill(data.begin(), data.end(), v); }
};

// Knobs that control the climate-driven parts of hydrology (winds, evaporation, etc.).
struct ClimateParams {
    // Sea level in the same units as HeightField values.
    float seaLevel = 0.0f;

    // Prevailing wind vector (east = +X, south = +Y).
    float windX = 1.0f;
    float windY = 0.0f;

    // Simple rainfall model controls.
    float baseEvaporation  = 0.005f; // base evaporation per pass
    float orographicFactor = 0.75f;  // strength of uplift → rain
    float rainShadow       = 0.25f;  // lee-side drying factor

    // Repeat passes to approximate seasonality.
    int   passes = 2;

    // Optional: temperature baseline (°C) at sea level and lapse rate (°C per height unit).
    // Assumes 1 height unit == 1 meter ⇒ -0.0065 °C/m ≈ -6.5 K/km.
    float baseTempC  = 15.0f;
    float lapseRate  = -0.0065f;

    // --- Legacy/back-compat fields used by StagesConfig.cpp (compile fix) ---
    // Units:
    //   - lapseRateKPerKm: Kelvin per kilometer (typical tropospheric mean ~6.5 K/km)
    //   - latGradientKPerDeg: Kelvin per degree latitude (applied by your model's sign/convention)
    //   - seaLevelTempK: Sea-level baseline temperature (Kelvin), default ≈ 288.15 K (15 °C)
    float lapseRateKPerKm    = 6.5f;
    float latGradientKPerDeg = 0.30f;
    float seaLevelTempK      = 288.15f;

    // Helper: if legacy Kelvin-based fields are provided by config, call this once
    // after loading to keep the canonical (Celsius) fields in sync.
    void applyLegacyOverrides() noexcept {
        baseTempC = seaLevelTempK - 273.15f;
        // Negative sign: cooler with altitude (K/km → °C/m).
        lapseRate = -(lapseRateKPerKm / 1000.0f);
    }
};

// Knobs that control river/lake identification and channel carving.
struct HydroParams {
    // Minimum fill depth (relative to base) to flag cells as lake.
    float lakeMinDepth   = 1.0f;

    // Flow accumulation threshold (in "cells") to classify rivers.
    float riverThreshold = 40.0f;

    // Gaussian blur sigma (in cells) for widening banks when carving channels.
    float bankWidth      = 1.5f;

    // Stream power law: E = K * A^m * S^n
    float incisionK      = 0.015f;  // K (erosion coefficient)
    float streamPowerM   = 0.5f;    // canonical m exponent
    float slopeExponentN = 1.0f;    // canonical n exponent

    // --- Legacy/back-compat fields used by StagesConfig.cpp (compile fix) ---
    // These mirror stream-power exponents m and n; some configs still write these names.
    float incisionExpM     = 0.5f;
    float incisionExpN     = 1.0f;

    // Optional post-incision smoothing passes (0 = disabled).
    int   smoothIterations = 0;

    // Helper: copy legacy exponent names into the canonical members if present.
    void applyLegacyOverrides() noexcept {
        streamPowerM   = incisionExpM;
        slopeExponentN = incisionExpN;
        if (smoothIterations < 0) smoothIterations = 0;
    }
};

// Outputs produced by the hydrology pipeline.
// All grids have the same dimensions as the input HeightField.
struct HydroOutputs {
    // Climate fields
    HeightField precip;      // precipitation (arbitrary units)
    HeightField temperature; // temperature (°C), if modeled by the pass

    // Terrain fields
    HeightField filled;      // depression-filled terrain (>= sea level)
    HeightField carved;      // terrain after channel incision
    HeightField waterLevel;  // water surface (sea/lake/river)

    // Flow diagnostics
    std::vector<std::uint8_t> flowDir;    // D8 primary direction per cell (0..7), 255 = no downslope
    std::vector<float>        flowAccum;  // flow accumulation / discharge proxy
    std::vector<std::uint8_t> riverMask;  // 1 = river, 0 = not river
    std::vector<std::uint8_t> lakeMask;   // 1 = lake, 0 = not lake

    // Convenience sentinel for "no downslope".
    static constexpr std::uint8_t FlowNone = 255u;
};

// Run the hydrology pipeline over a base (raw) heightfield.
// Returns precipitation, filled/carved surfaces, water surface, and flow diagnostics.
[[nodiscard]] HydroOutputs runHydrology(const HeightField& baseHeight,
                                        const ClimateParams& climate,
                                        const HydroParams& hydro);

} // namespace cg
