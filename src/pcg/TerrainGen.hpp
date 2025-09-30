#pragma once
#include <vector>
#include <cstdint>
#include <string_view>
#include "SeededRng.hpp"
#include "Noise.hpp"
#include "Biomes.hpp"

namespace pcg {

struct ChunkCoord { int cx, cy; };

struct TerrainParams {
    int   size = 256;               // cells per side
    float scale = 0.004f;           // world-to-noise scale
    int   octaves = 5;
    float lacunarity = 2.0f;
    float gain = 0.5f;

    // elevation shaping
    float baseHeight = 0.0f;
    float elevationAmp = 60.0f;     // meters
    float continentFreq = 0.0008f;  // lower -> bigger continents
    float ridgeWeight = 0.35f;

    // climate
    float tempLapseRate = 0.0065f;  // per meter
    float moistureBias = 0.0f;
};

struct TerrainChunk {
    int size;
    float cellSize;                // world units per cell
    std::vector<float> height;     // size*size
    std::vector<float> temp;       // 0..1
    std::vector<float> moisture;   // 0..1
    std::vector<float> flow;       // flow accumulation
    std::vector<uint8_t> rivers;   // 0/1 mask
    std::vector<Biome> biomes;     // per cell
};

TerrainChunk generate_terrain(uint64_t worldSeed, ChunkCoord cc, float cellSize, const TerrainParams& p);

} // namespace pcg
