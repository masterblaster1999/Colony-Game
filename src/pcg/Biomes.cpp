#include "Biomes.hpp"

namespace pcg {

Biome classify_biome(float t, float m, const BiomeParams& b) {
    // Very simple grid; adjust to taste
    if (t < b.cold) {
        if (m < b.wet1) return Biome::Tundra;
        if (m < b.wet2) return Biome::Shrubland;
        return Biome::BorealForest;
    } else if (t < b.cool) {
        if (m < b.wet1) return Biome::Shrubland;
        if (m < b.wet2) return Biome::Grassland;
        return Biome::TemperateForest;
    } else if (t < b.warm) {
        if (m < b.wet1) return Biome::Desert;
        if (m < b.wet2) return Biome::Grassland;
        return Biome::TemperateRainforest;
    } else { // hot
        if (m < b.wet1) return Biome::Desert;
        if (m < b.wet2) return Biome::Savanna;
        return Biome::TropicalRainforest;
    }
}

} // namespace pcg
