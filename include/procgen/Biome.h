#pragma once
#include <cstdint>

namespace procgen {

enum class Biome : uint8_t {
    Ocean, Beach, Grassland, Forest, Desert, Savanna, Taiga, Tundra, Mountain, Snow
};

inline Biome classify_biome(float elev, float moisture, float tempC) {
    if (elev < 0.02f) return Biome::Ocean;
    if (elev < 0.06f) return Biome::Beach;

    if (elev > 0.75f) return (tempC < -5.0f) ? Biome::Snow : Biome::Mountain;

    if (tempC < -5.0f) return Biome::Tundra;
    if (tempC < 5.0f)  return (moisture > 0.5f) ? Biome::Taiga : Biome::Tundra;
    if (tempC < 18.0f) {
        if (moisture < 0.25f) return Biome::Desert;
        if (moisture < 0.55f) return Biome::Grassland;
        return Biome::Forest;
    }
    // warm
    if (moisture < 0.25f) return Biome::Desert;
    if (moisture < 0.6f)  return Biome::Savanna;
    return Biome::Forest;
}

} // namespace procgen
