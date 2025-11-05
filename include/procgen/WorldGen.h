#pragma once
#include <vector>
#include <cstdint>
#include "Heightmap.h"
#include "Biome.h"

namespace procgen {

struct WorldGenParams {
    uint32_t seed = 12345;
    int width = 512;
    int height = 512;

    // elevation
    int octaves = 6; float lacunarity = 2.0f; float gain = 0.5f;
    float baseFreq = 0.0035f;
    float warpFreq = 0.01f; float warpAmp = 2.0f;
    float ridgeSharpness = 1.25f; // >1 sharpens mountains

    // classification thresholds
    float seaLevel = 0.45f;
};

struct GeneratedWorld {
    Heightmap elevation;              // [0..1]
    std::vector<float> moisture;      // [0..1], size = W*H
    std::vector<float> temperatureC;  // degrees C, size = W*H
    std::vector<uint8_t> biomes;      // Biome enum values
};

class WorldGenerator {
public:
    static GeneratedWorld generate(const WorldGenParams& p);
};

} // namespace procgen
