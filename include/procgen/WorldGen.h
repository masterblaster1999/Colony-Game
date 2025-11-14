#pragma once

#include <cstdint>
#include <vector>

#include "Heightmap.h"
#include "Biome.h"

namespace procgen {

// Large-scale region type for each part of the world.
enum class RegionKind : std::uint8_t
{
    Highlands,
    Plains,
    Desert,
    Wetlands,
    Archipelago,
    Rift,
    Plateau,
};

struct WorldGenParams
{
    std::uint32_t seed = 12345;

    int width  = 512;
    int height = 512;

    // Elevation noise
    int   octaves        = 6;
    float lacunarity     = 2.0f;
    float gain           = 0.5f;
    float baseFreq       = 0.0035f;
    float warpFreq       = 0.01f;
    float warpAmp        = 2.0f;
    float ridgeSharpness = 1.25f; // >1 sharpens mountains

    // Classification thresholds
    float seaLevel = 0.45f;

    // --- NEW: regional archetypes controls ---
    // Coarse grid size: each cell gets an archetype (Highlands, Desert, …).
    int   regionsX = 8;
    int   regionsY = 4;

    // Reserved for softer transitions later (0 = hard borders).
    float regionBlend = 0.0f;
};

struct GeneratedWorld
{
    Heightmap elevation;              // [0..1]
    std::vector<float>    moisture;   // [0..1], size = W*H
    std::vector<float>    temperatureC; // degrees C, size = W*H
    std::vector<std::uint8_t> biomes; // Biome enum values

    // NEW: archetype at each tile (same indexing as moisture/temperatureC)
    std::vector<RegionKind> regions;
};

class WorldGenerator
{
public:
    static GeneratedWorld generate(const WorldGenParams& p);
};

} // namespace procgen
