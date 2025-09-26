#pragma once
// -----------------------------------------------------------------------------
// src/worldgen/Biomes.hpp
// Biome classification (Whittaker-style) + 4-channel terrain splat masks
// (grass, rock, sand, snow). Designed to consume your Hydrology outputs:
//   - temperature (°C)
//   - precip     (unitless; scale to mm/yr with precipScaleToMm)
//   - height     (world Z; compare to seaLevel)
// References: Whittaker biome diagram; Red Blob mapgen rainfall/biomes. :contentReference[oaicite:6]{index=6}
// -----------------------------------------------------------------------------

#include <cstdint>
#include <vector>
#include "DomainWarp.hpp"   // HeightField + computeSlopeMap (from your earlier module)

namespace cg {

enum class BiomeId : uint8_t {
    Ocean=0, Lake=1,
    Desert=2, Shrubland=3, Savanna=4, Grassland=5,
    TemperateForest=6, BorealForest=7, Tundra=8, TropicalRainforest=9, Alpine=10
};

struct BiomeParams {
    // Convert your precipitation units to mm/year (tune this to your model)
    float precipScaleToMm = 1200.0f; // 1.0 precip unit == 1200 mm/yr (example)

    // Temperature (°C) breakpoints ~Whittaker-like
    float t_cold   = -5.0f;
    float t_cool   = 5.0f;
    float t_warm   = 15.0f;
    float t_hot    = 25.0f;

    // Precipitation (mm/yr) breakpoints
    float p_dry      = 250.0f;
    float p_semiarid = 500.0f;
    float p_subhumid = 1000.0f;
    float p_humid    = 2000.0f;

    // Elevation & slope heuristics
    float alpineHeight        = 300.0f; // above sea by this → Alpine
    float beachHeightRange    = 3.0f;   // |z - seaLevel| < range → beach-like (sand)
    float rockSlopeStartDeg   = 28.0f;  // slope in degrees where rock starts to dominate
    float rockSlopeFullDeg    = 45.0f;  // slope in degrees where rock fully dominates
    float snowTempC           = -2.0f;  // below this → snow weight rises
    float snowHeightBonus     = 200.0f; // extra snow push above this (world units)
};

struct BiomeOutputs {
    int w=0, h=0;
    std::vector<uint8_t> biomeId; // size w*h

    // Splat weights as four single-channel heightfields (float 0..1)
    HeightField maskGrass;  // G
    HeightField maskRock;   // R
    HeightField maskSand;   // B
    HeightField maskSnow;   // A
};

// Classify into a BiomeId from temperature (°C), precip (mm/yr) & height (Z)
BiomeId classifyBiome(float tempC, float precipMm, float heightZ,
                      float seaLevel, const BiomeParams& B);

// Build full biome map and splat masks from temperature/precip/height.
// xyScale and zScale should match your terrain mesh scales (for slope).
BiomeOutputs buildBiomes(const HeightField& temperatureC,
                         const HeightField& precipUnits,
                         const HeightField& heightZ,
                         float seaLevel,
                         float xyScale, float zScale,
                         const BiomeParams& B);

// Debug: readable biome name
const char* biomeName(BiomeId id);

} // namespace cg
