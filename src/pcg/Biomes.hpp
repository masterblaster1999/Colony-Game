#pragma once
#include <cstdint>

namespace pcg {

enum class Biome : uint8_t {
    Tundra, BorealForest, TemperateForest, Grassland, Savanna,
    Desert, Shrubland, TropicalRainforest, TemperateRainforest, Alpine
};

struct BiomeParams {
    // thresholds are 0..1 normalized
    float cold = 0.25f;
    float cool = 0.45f;
    float warm = 0.65f;
    float wet1 = 0.3f;
    float wet2 = 0.6f;
};

Biome classify_biome(float temp01, float moist01, const BiomeParams& bp);

} // namespace pcg
