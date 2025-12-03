// src/worldgen/stages/Hydrology.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/WorldChunk.hpp"  // <-- FIX: bring full WorldChunk definition into this TU
#include <algorithm>
#include <utility>
#include <vector>
#include <cstdint>

namespace colony::worldgen {

void HydrologyStage::generate(StageContext& ctx)
{
    // Initialize references at declaration time
    auto& H = ctx.out.height;
    auto& F = ctx.out.flow;

    const int N = H.width(); // If your grid lacks width(), use ctx.settings.cellsPerChunk

    // Process cells from high -> low so water always flows downhill deterministically
    std::vector<std::pair<int,int>> order;
    order.reserve(static_cast<std::size_t>(N) * static_cast<std::size_t>(N));
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            order.emplace_back(x, y);

    std::sort(order.begin(), order.end(),
              [&](const auto& a, const auto& b) {
                  return H.at(a.first, a.second) > H.at(b.first, b.second);
              });

    // 8-neighborhood for steepest-descent routing
    constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dy[8] = {-1,-1,-1,  0, 0,  1, 1, 1};

    for (auto [x, y] : order) {
        // Base flux (rain) + any upstream flow already accumulated
        const float fluxBase = 0.002f;
        float flux = fluxBase + F.at(x, y);

        float bestDrop = 0.f; int bx = -1, by = -1;
        const float h = H.at(x, y);

        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            const float drop = h - H.at(nx, ny);
            if (drop > bestDrop) { bestDrop = drop; bx = nx; by = ny; }
        }

        if (bx >= 0) {
            // Send the water to the steepest neighbor
            F.at(bx, by) += flux;

            // Slight channel carving where large flux occurs (toy erosion)
            if (flux > 0.02f) {
                const float newH = H.at(x, y) - bestDrop * 0.05f;
                H.at(x, y) = (newH > 0.f) ? newH : 0.f;
            }
        }
    }
}

} // namespace colony::worldgen
