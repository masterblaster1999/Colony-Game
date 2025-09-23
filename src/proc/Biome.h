#pragma once
#include <cstdint>
enum class Biome : uint8_t {
    Tundra, BorealForest, TemperateForest, Grassland, Savanna, Desert, TropicalForest
};

inline Biome ClassifyBiome(float tempC, float precipMm) {
    // Very simplified Whittaker-style thresholds (tweak to taste).
    if (precipMm < 200) return Biome::Desert;
    if (tempC < -5)     return Biome::Tundra;
    if (tempC < 5)      return (precipMm > 700) ? Biome::BorealForest : Biome::Grassland;
    if (tempC < 20)     return (precipMm > 800) ? Biome::TemperateForest : Biome::Grassland;
    return (precipMm > 1500) ? Biome::TropicalForest : Biome::Savanna;
}
