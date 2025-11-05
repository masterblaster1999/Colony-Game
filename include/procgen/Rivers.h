#pragma once
#include "Heightmap.h"
#include <vector>

namespace procgen {

struct River {
    std::vector<IV2> path; // from source -> mouth
};

struct RiverParams {
    int maxRivers = 64;
    int minSourceElevationCell = 200;   // rank threshold (0 = lowest, W*H-1 = highest)
    float carveDepth = 0.015f;
    int maxLen = 5000;
};

std::vector<River> generate_rivers(Heightmap& elevation, float seaLevel, const RiverParams& p);

} // namespace procgen
