#include "procgen/RegionArchetypes.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace procgen {

namespace {

// Simple parameterisation for each archetype.
struct RegionArchetypeDef
{
    RegionKind kind;
    float moistureBias;    // added to [0..1] moisture
    float temperatureBias; // added in °C
};

RegionArchetypeDef get_def(RegionKind k)
{
    switch (k)
    {
    case RegionKind::Highlands:
        // colder, a bit drier
        return { k, -0.10f, -6.0f };
    case RegionKind::Plains:
        return { k, 0.0f, 0.0f };
    case RegionKind::Desert:
        // hot and very dry
        return { k, -0.35f, +8.0f };
    case RegionKind::Wetlands:
        // warm and humid
        return { k, +0.35f, +2.0f };
    case RegionKind::Archipelago:
        // coastal, a bit humid and warm
        return { k, +0.15f, +1.0f };
    case RegionKind::Rift:
        // extreme climate: hot and a touch dry
        return { k, -0.05f, +4.0f };
    case RegionKind::Plateau:
        // elevated but not as extreme as Highlands
        return { k, -0.05f, -1.0f };
    }

    // Fallback (should never hit)
    return { RegionKind::Plains, 0.0f, 0.0f };
}

// Lightweight weighted pick for a region kind given latitude [0,1] (0 = top, 1 = bottom).
RegionKind pick_region_for_lat(float lat01, std::mt19937& rng)
{
    lat01 = std::clamp(lat01, 0.0f, 1.0f);

    // 0 at equator, 0.5 near poles.
    const float poleBias    = std::abs(lat01 - 0.5f);
    const float equatorBias = 1.0f - poleBias;

    float wHighlands   = 0.2f + 0.8f * poleBias;
    float wPlains      = 0.4f;
    float wDesert      = 0.1f + 0.5f * equatorBias;
    float wWetlands    = 0.1f + 0.4f * equatorBias;
    float wArchipelago = 0.1f;
    float wRift        = 0.1f * equatorBias;
    float wPlateau     = 0.1f * poleBias;

    // Small jitter so rows don’t look perfectly straight.
    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);
    wHighlands   = std::max(0.0f, wHighlands   + jitter(rng));
    wPlains      = std::max(0.0f, wPlains      + jitter(rng));
    wDesert      = std::max(0.0f, wDesert      + jitter(rng));
    wWetlands    = std::max(0.0f, wWetlands    + jitter(rng));
    wArchipelago = std::max(0.0f, wArchipelago + jitter(rng));
    wRift        = std::max(0.0f, wRift        + jitter(rng));
    wPlateau     = std::max(0.0f, wPlateau     + jitter(rng));

    const float total =
        wHighlands + wPlains + wDesert + wWetlands +
        wArchipelago + wRift + wPlateau;

    if (total <= 0.0f)
        return RegionKind::Plains;

    std::uniform_real_distribution<float> unit(0.0f, total);
    float t = unit(rng);

    auto step = [&](float w) {
        if (t < w) return true;
        t -= w;
        return false;
    };

    if (step(wHighlands))   return RegionKind::Highlands;
    if (step(wPlains))      return RegionKind::Plains;
    if (step(wDesert))      return RegionKind::Desert;
    if (step(wWetlands))    return RegionKind::Wetlands;
    if (step(wArchipelago)) return RegionKind::Archipelago;
    if (step(wRift))        return RegionKind::Rift;
    return RegionKind::Plateau;
}

} // namespace

void assign_regions(GeneratedWorld& world, const WorldGenParams& params)
{
    const int width  = params.width;
    const int height = params.height;

    if (width <= 0 || height <= 0)
    {
        world.regions.clear();
        return;
    }

    const std::size_t tileCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    // Make sure the world arrays are sized as expected.
    if (world.moisture.size() != tileCount ||
        world.temperatureC.size() != tileCount)
    {
        // Something went wrong upstream; don't crash.
        world.regions.assign(tileCount, RegionKind::Plains);
        return;
    }

    world.regions.assign(tileCount, RegionKind::Plains);

    const int cellsX = std::max(1, params.regionsX);
    const int cellsY = std::max(1, params.regionsY);

    // Deterministic RNG based on seed.
    std::mt19937 rng(
        static_cast<std::mt19937::result_type>(
            params.seed ^ 0x9E3779B9u));

    // 1) Choose an archetype for each coarse region cell.
    std::vector<RegionKind> cellKinds(
        static_cast<std::size_t>(cellsX * cellsY));

    for (int cy = 0; cy < cellsY; ++cy)
    {
        // Map row index to [0,1] latitude (0 = top, 1 = bottom).
        float lat01 = (cellsY > 1)
            ? static_cast<float>(cy) / static_cast<float>(cellsY - 1)
            : 0.5f;

        for (int cx = 0; cx < cellsX; ++cx)
        {
            const int idx = cy * cellsX + cx;
            cellKinds[static_cast<std::size_t>(idx)] =
                pick_region_for_lat(lat01, rng);
        }
    }

    // 2) For each tile, map it to a coarse cell and apply climate tweaks.
    for (int y = 0; y < height; ++y)
    {
        const int cellY = std::clamp(y * cellsY / height, 0, cellsY - 1);

        for (int x = 0; x < width; ++x)
        {
            const int cellX = std::clamp(x * cellsX / width, 0, cellsX - 1);
            const int cellIdx = cellY * cellsX + cellX;

            const std::size_t i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            RegionKind kind =
                cellKinds[static_cast<std::size_t>(cellIdx)];
            world.regions[i] = kind;

            const RegionArchetypeDef def = get_def(kind);

            // Moisture stays clamped to [0,1]
            float m = world.moisture[i] + def.moistureBias;
            world.moisture[i] = std::clamp(m, 0.0f, 1.0f);

            world.temperatureC[i] += def.temperatureBias;
        }
    }
}

} // namespace procgen
