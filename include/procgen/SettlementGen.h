#pragma once
#include <vector>
#include "Types.h"
#include "Heightmap.h"

namespace procgen {

struct Road { std::vector<IV2> cells; };
struct Plot { IV2 cell; int w=1, h=1; };

struct Settlement {
    std::vector<Road> roads;
    std::vector<Plot> plots;
};

struct SettlementParams {
    uint32_t seed = 42;
    int targetSites = 32;
    float slopeCost = 40.0f; // penalty for steep tiles
};

Settlement generate_settlement(const Heightmap& elev, float seaLevel, const SettlementParams& p);

} // namespace procgen
