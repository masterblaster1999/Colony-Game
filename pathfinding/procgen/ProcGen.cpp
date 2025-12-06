// pathfinding/procgen/ProcGen.cpp
#include "ProcGen.h"
#include "Noise.h"
#include "Cellular.h"
#include "RoadCarver.h"
#include <algorithm>
#include <cmath>

namespace colony::pathfinding::procgen {

ProcGenOutputs GeneratePathfindingFields(const ProcGenSettings& cfg)
{
    const int W = cfg.width, H = cfg.height;

    // 1) Start with random obstacles, then smooth via cellular automata
    std::vector<uint8_t> obstacle;
    randomMask(obstacle, W, H, cfg.obstacle_density, cfg.seed);
    for (int i = 0; i < cfg.cellular_steps; ++i) cellularStep(obstacle, W, H);

    // 2) Build a continuous movement-cost field from noise (fBm)
    std::vector<uint16_t> cost(static_cast<size_t>(W) * H, 1);
    const uint32_t noiseseed = cfg.seed ^ 0xA5A5A5A5u;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float nx = static_cast<float>(x) * cfg.noise_scale;
            float ny = static_cast<float>(y) * cfg.noise_scale;
            float n  = fbm2D(nx, ny, noiseseed, cfg.noise_octaves, cfg.noise_lacunarity, cfg.noise_gain); // [0,1]
            // Map noise to [1..max_cost]
            uint16_t c = static_cast<uint16_t>(1 + std::round(n * (cfg.max_cost - 1)));
            cost[I(x, y, W)] = std::clamp<uint16_t>(c, 1, cfg.max_cost);

            // Optional: water as hard obstacle (below water level), but keep a few openings
            if (n < cfg.water_level && ((x + y) % 9)) {
                obstacle[I(x, y, W)] = 1u;
            }
        }
    }

    // 3) Place well‑spaced POIs with Poisson disk sampling
    auto pois = poissonDisk(W, H, cfg.poisson_min_dist, cfg.poisson_attempts, cfg.seed ^ 0x5EED5EEDu);

    // Nudge POIs off solid tiles if needed
    for (auto& p : pois) {
        if (!obstacle[I(p.x, p.y, W)]) continue;
        // Spiral search up to r=6 to find a free spot
        bool moved = false;
        for (int r = 1; r <= 6 && !moved; ++r) {
            for (int dy = -r; dy <= r && !moved; ++dy)
            for (int dx = -r; dx <= r && !moved; ++dx) {
                int nx = p.x + dx, ny = p.y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                if (!obstacle[I(nx, ny, W)]) { p.x = nx; p.y = ny; moved = true; }
            }
        }
    }

    // 4) Carve roads (clear obstacles + lower costs) along MST A* connections between POIs
    GridEdit edit{ W, H, &obstacle, &cost };
    carveRoads(edit, pois, cfg.road_cost, cfg.road_radius);

    return ProcGenOutputs{ W, H, std::move(obstacle), std::move(cost), std::move(pois) };
}

} // namespace colony::pathfinding::procgen
