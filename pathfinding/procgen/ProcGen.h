// pathfinding/procgen/ProcGen.h
#pragma once
#include <vector>
#include <cstdint>
#include "PoissonDisk.h"

namespace colony::pathfinding::procgen {

struct ProcGenSettings {
    int width = 256;
    int height = 256;
    uint32_t seed = 1337u;

    // Obstacle field
    float obstacle_density = 0.38f;  // initial random fill
    int cellular_steps = 3;          // smoothing passes

    // Cost field from noise
    float noise_scale = 0.08f;       // smaller = larger features
    int   noise_octaves = 4;
    float noise_lacunarity = 2.0f;
    float noise_gain = 0.5f;
    float water_level = 0.18f;       // if noise < water_level -> obstacle (shallow lakes/rivers)
    uint16_t max_cost = 12;          // 1..max_cost

    // POIs & roads
    float poisson_min_dist = 24.0f;  // spacing between POIs
    int   poisson_attempts = 16;
    uint16_t road_cost = 1;
    int road_radius = 1;             // thickness
};

struct ProcGenOutputs {
    int width, height;
    std::vector<uint8_t> obstacleMask;  // 1 = blocked, 0 = free
    std::vector<uint16_t> costField;    // movement cost >= 1
    std::vector<Int2> pois;             // waypoints used for roads; handy for AI too
};

ProcGenOutputs GeneratePathfindingFields(const ProcGenSettings& cfg);

} // namespace colony::pathfinding::procgen
