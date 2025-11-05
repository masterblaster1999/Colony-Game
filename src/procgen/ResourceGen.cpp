#include "procgen/ResourceGen.h"
#include "procgen/PoissonDisk.h"
#include <random>

namespace procgen {

static bool biome_allows(uint8_t b, uint8_t kind){
    auto bb = (Biome)b;
    switch(kind){
        case 0: return bb==Biome::Forest || bb==Biome::Taiga || bb==Biome::Savanna; // wood
        case 1: return bb==Biome::Grassland || bb==Biome::Forest || bb==Biome::Savanna; // game
        case 2: return bb==Biome::Mountain || bb==Biome::Taiga || bb==Biome::Tundra; // ore
        case 3: return bb==Biome::Desert || bb==Biome::Savanna; // oil-ish
        default: return true;
    }
}

std::vector<ResourceSite> generate_resources(
    const std::vector<uint8_t>& biomes, int w, int h, const ResourceGenParams& p)
{
    std::vector<ResourceSite> out;
    auto pts = poisson_disk_2d(0.02f, 30, p.seed); // in unit coords
    std::mt19937 rng(p.seed);
    std::uniform_int_distribution<int> kindDist(0,3);
    std::uniform_real_distribution<float> rich(0.5f, 1.0f);

    for (auto v : pts){
        int x = (int)(v.x * (w-1));
        int y = (int)(v.y * (h-1));
        uint8_t kind = (uint8_t)kindDist(rng);
        if (!biome_allows(biomes[y*w + x], kind)) continue;
        out.push_back({ {x,y}, kind, rich(rng) });
    }
    return out;
}

} // namespace procgen
