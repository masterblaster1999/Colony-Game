#pragma once

#include <vector>
#include <random>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

// Simple procedural colony map generator for a 2D grid.
// Header-only by design: just include this file where you need it.
//
// Typical usage:
//
//   #include "ColonyMapGenerator.hpp"
//
//   colony::procgen::ProcGenParams params;
//   params.width  = 256;
//   params.height = 256;
//   params.seed   = 12345;
//
//   colony::procgen::GeneratedMap map =
//       colony::procgen::generateColonyMap(params);
//
//   // Use map.cells[y * map.width + x] inside your world setup code.

namespace colony::procgen {

// --- Basic enumerations -----------------------------------------------------

enum class TerrainType : std::uint8_t
{
    Ocean = 0,
    Coast,
    Plains,
    Forest,
    Hill,
    Mountain,
    Desert,
    Snow
};

enum class ResourceType : std::uint8_t
{
    None = 0,
    Wood,
    Stone,
    Iron,
    Food
};

// --- Parameters & result types ----------------------------------------------

struct ProcGenParams
{
    int width  = 128;
    int height = 128;

    // Seed for map generation. If zero, a random device will be used.
    std::uint32_t seed = 0;

    // Height thresholds (0..1).
    float waterLevel     = 0.30f; // below this: ocean
    float coastBand      = 0.05f; // just above waterLevel: coast
    float hillLevel      = 0.60f;
    float mountainLevel  = 0.80f;
    float snowLevel      = 0.92f;

    // Moisture thresholds (0..1).
    float desertMoisture  = 0.25f;
    float forestMoisture  = 0.55f;

    // Noise scaling.
    float heightFrequency   = 1.5f;  // smaller -> larger features
    float moistureFrequency = 2.0f;

    int   heightOctaves     = 4;
    float heightPersistence = 0.5f;
    int   moistureOctaves   = 3;
    float moisturePersistence = 0.6f;

    // Resource density factors (0..1, interpreted as probabilities).
    float forestWoodChance    = 0.70f;
    float hillStoneChance     = 0.25f;
    float mountainIronChance  = 0.35f;
    float plainsFoodChance    = 0.15f;
};

struct Cell
{
    float        height   = 0.0f; // 0..1
    float        moisture = 0.0f; // 0..1
    TerrainType  terrain  = TerrainType::Ocean;
    ResourceType resource = ResourceType::None;
};

struct GeneratedMap
{
    int width  = 0;
    int height = 0;

    // row-major: cells[y * width + x]
    std::vector<Cell> cells;

    // A suggested colony start tile (guaranteed to be on non-water terrain
    // if such a cell exists).
    int startX = 0;
    int startY = 0;

    [[nodiscard]] bool inBounds(int x, int y) const noexcept
    {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    [[nodiscard]] const Cell& cell(int x, int y) const noexcept
    {
        return cells[static_cast<std::size_t>(y) * width +
                     static_cast<std::size_t>(x)];
    }

    Cell& cell(int x, int y) noexcept
    {
        return cells[static_cast<std::size_t>(y) * width +
                     static_cast<std::size_t>(x)];
    }
};

// --- Internal helpers (detail namespace) ------------------------------------

namespace detail {

// Simple hashed 2D value noise on integer grid.
inline float valueNoise2D(int x, int y, std::uint32_t seed) noexcept
{
    // 32-bit integer hash (xorshift-like).
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
                      static_cast<std::uint32_t>(y) * 0xd8163841u ^
                      seed * 0xcb1ab31fu;

    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;

    // Map to [0, 1].
    return static_cast<float>(h) / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
}

inline float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

inline float smoothstep(float t) noexcept
{
    // Smooth interpolation curve: 3t^2 - 2t^3
    return t * t * (3.0f - 2.0f * t);
}

// Bilinear interpolation over valueNoise2D.
inline float valueNoise2D(float x, float y, std::uint32_t seed) noexcept
{
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = smoothstep(x - static_cast<float>(x0));
    float fy = smoothstep(y - static_cast<float>(y0));

    float v00 = valueNoise2D(x0, y0, seed);
    float v10 = valueNoise2D(x1, y0, seed);
    float v01 = valueNoise2D(x0, y1, seed);
    float v11 = valueNoise2D(x1, y1, seed);

    float vx0 = lerp(v00, v10, fx);
    float vx1 = lerp(v01, v11, fx);

    return lerp(vx0, vx1, fy);
}

// Fractal Brownian Motion over value noise (sum of octaves).
inline float fbm2D(float x, float y,
                   int octaves,
                   float persistence,
                   float lacunarity,
                   std::uint32_t seed) noexcept
{
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float sum       = 0.0f;
    float maxSum    = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        float n = valueNoise2D(x * frequency, y * frequency,
                               seed + static_cast<std::uint32_t>(i) * 9973u);
        sum    += n * amplitude;
        maxSum += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    if (maxSum > 0.0f)
        sum /= maxSum;

    // Clamp to [0,1].
    if (sum < 0.0f) sum = 0.0f;
    if (sum > 1.0f) sum = 1.0f;

    return sum;
}

// Determine terrain from height and moisture.
inline TerrainType classifyTerrain(float height,
                                   float moisture,
                                   const ProcGenParams& p) noexcept
{
    if (height < p.waterLevel)
        return TerrainType::Ocean;

    if (height < p.waterLevel + p.coastBand)
        return TerrainType::Coast;

    // Above waterline.
    if (height > p.snowLevel)
        return TerrainType::Snow;

    if (height > p.mountainLevel)
        return TerrainType::Mountain;

    if (height > p.hillLevel)
        return TerrainType::Hill;

    // Lowlands: differentiate by moisture.
    if (moisture < p.desertMoisture)
        return TerrainType::Desert;

    if (moisture > p.forestMoisture)
        return TerrainType::Forest;

    return TerrainType::Plains;
}

// Place resources based on terrain & random chance.
inline ResourceType pickResource(TerrainType terrain,
                                 float rand01,
                                 const ProcGenParams& p) noexcept
{
    switch (terrain)
    {
    case TerrainType::Forest:
        if (rand01 < p.forestWoodChance)
            return ResourceType::Wood;
        break;

    case TerrainType::Hill:
        if (rand01 < p.hillStoneChance)
            return ResourceType::Stone;
        break;

    case TerrainType::Mountain:
        if (rand01 < p.mountainIronChance)
            return ResourceType::Iron;
        break;

    case TerrainType::Plains:
        if (rand01 < p.plainsFoodChance)
            return ResourceType::Food;
        break;

    default:
        break;
    }

    return ResourceType::None;
}

// Choose a "nice" starting position: non-water, non-extreme height,
// prefer central area and moderate moisture.
inline void chooseStartPosition(const GeneratedMap& map,
                                int& outX,
                                int& outY) noexcept
{
    float bestScore = -1.0f;
    int   bestX     = map.width / 2;
    int   bestY     = map.height / 2;

    // Avoid map borders for start positions: restrict to inner rectangle.
    int minX = map.width  / 6;
    int maxX = (map.width  * 5) / 6;
    int minY = map.height / 6;
    int maxY = (map.height * 5) / 6;

    for (int y = minY; y < maxY; ++y)
    {
        for (int x = minX; x < maxX; ++x)
        {
            const Cell& c = map.cell(x, y);

            // Skip water & extreme terrain.
            if (c.terrain == TerrainType::Ocean ||
                c.terrain == TerrainType::Coast ||
                c.terrain == TerrainType::Mountain ||
                c.terrain == TerrainType::Snow)
            {
                continue;
            }

            // Score based on:
            // - height close to mid-range (0.5)
            // - moisture neither too dry nor swampy (~0.4-0.7).
            float heightScore   = 1.0f - std::fabs(c.height - 0.5f) * 2.0f;
            float moistureScore = 1.0f - std::fabs(c.moisture - 0.5f) * 2.0f;

            float score = (heightScore * 0.6f) + (moistureScore * 0.4f);

            if (score > bestScore)
            {
                bestScore = score;
                bestX     = x;
                bestY     = y;
            }
        }
    }

    outX = bestX;
    outY = bestY;
}

} // namespace detail

// --- Main generation function -----------------------------------------------

inline GeneratedMap generateColonyMap(const ProcGenParams& params)
{
    GeneratedMap result;
    result.width  = std::max(1, params.width);
    result.height = std::max(1, params.height);
    result.cells.resize(static_cast<std::size_t>(result.width * result.height));

    // Seed RNG.
    std::uint32_t seed = params.seed;
    if (seed == 0)
    {
        std::random_device rd;
        seed = (static_cast<std::uint32_t>(rd()) << 16) ^ static_cast<std::uint32_t>(rd());
        if (seed == 0) seed = 1; // avoid all-zero seed
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni01(0.0f, 1.0f);

    const float invWidth  = 1.0f / std::max(1, params.width - 1);
    const float invHeight = 1.0f / std::max(1, params.height - 1);

    // 1) Generate base height & moisture fields using FBM value noise.
    for (int y = 0; y < result.height; ++y)
    {
        float ny = static_cast<float>(y) * invHeight;
        for (int x = 0; x < result.width; ++x)
        {
            float nx = static_cast<float>(x) * invWidth;

            // Apply a slight bias to encourage land in the centre of the map.
            float dx = nx - 0.5f;
            float dy = ny - 0.5f;
            float distanceFromCenter = std::sqrt(dx * dx + dy * dy);

            float heightBase = detail::fbm2D(
                nx * params.heightFrequency,
                ny * params.heightFrequency,
                params.heightOctaves,
                params.heightPersistence,
                2.0f, // lacunarity
                seed + 1337u
            );

            // Push the center up, edges down.
            float islandMask = 1.0f - distanceFromCenter * 1.1f;
            islandMask = std::clamp(islandMask, 0.0f, 1.0f);

            float height = heightBase * 0.7f + islandMask * 0.3f;

            float moisture = detail::fbm2D(
                nx * params.moistureFrequency,
                ny * params.moistureFrequency,
                params.moistureOctaves,
                params.moisturePersistence,
                2.0f,
                seed + 7331u
            );

            Cell& c = result.cell(x, y);
            c.height   = std::clamp(height,   0.0f, 1.0f);
            c.moisture = std::clamp(moisture, 0.0f, 1.0f);
        }
    }

    // 2) Classify terrain and assign resources.
    for (int y = 0; y < result.height; ++y)
    {
        for (int x = 0; x < result.width; ++x)
        {
            Cell& c = result.cell(x, y);

            c.terrain = detail::classifyTerrain(
                c.height,
                c.moisture,
                params
            );

            float r = uni01(rng);
            c.resource = detail::pickResource(c.terrain, r, params);
        }
    }

    // 3) Slight smoothing of coastlines (optional quick pass).
    // Turn isolated coast tiles into ocean or land to reduce noise.
    for (int y = 1; y < result.height - 1; ++y)
    {
        for (int x = 1; x < result.width - 1; ++x)
        {
            Cell& c = result.cell(x, y);
            if (c.terrain != TerrainType::Coast)
                continue;

            int waterNeighbors = 0;
            int landNeighbors  = 0;

            for (int oy = -1; oy <= 1; ++oy)
            {
                for (int ox = -1; ox <= 1; ++ox)
                {
                    if (ox == 0 && oy == 0) continue;
                    const Cell& n = result.cell(x + ox, y + oy);
                    if (n.terrain == TerrainType::Ocean)
                        ++waterNeighbors;
                    else
                        ++landNeighbors;
                }
            }

            if (waterNeighbors >= 6)
                c.terrain = TerrainType::Ocean;
            else if (landNeighbors >= 6)
                c.terrain = TerrainType::Plains;
        }
    }

    // 4) Pick start position.
    detail::chooseStartPosition(result, result.startX, result.startY);

    return result;
}

} // namespace colony::procgen
