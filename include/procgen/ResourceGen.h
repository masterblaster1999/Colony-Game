#pragma once
#include <vector>
#include <cstdint>
#include "Biome.h"
#include "Types.h"

namespace procgen {

struct ResourceSite { IV2 cell; uint8_t kind; float richness; };

struct ResourceGenParams {
    float baseDensity = 0.0025f; // per tile
    uint32_t seed = 777;
};

std::vector<ResourceSite> generate_resources(
    const std::vector<uint8_t>& biomes, int w, int h, const ResourceGenParams& p);

} // namespace procgen
