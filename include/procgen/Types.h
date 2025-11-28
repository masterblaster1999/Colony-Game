#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace procgen {

struct Vec2 { float x = 0.f, y = 0.f; };

// --- Back-compat shims (keep until all call sites are migrated) ---
struct IV2 { int x = 0, y = 0; };

// Your new 'Vec2' is the float vector. Keep the old alias alive:
using FV2 = Vec2;

// Keep the old helper (many call sites rely on it in Rivers and friends)
constexpr inline bool in_bounds(int x, int y, int w, int h) noexcept {
    return static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
           static_cast<unsigned>(y) < static_cast<unsigned>(h);
}
// -------------------------------------------------------------------

struct Color {
    uint8_t r=0, g=0, b=0, a=255;
};

enum struct Biome : uint8_t {
    Ocean = 0, Beach, Desert, Grassland, Forest, Rainforest,
    Savanna, Taiga, Tundra, Snow, Mountain
};

enum struct ResourceType : uint8_t {
    Tree=0, Stone, OreIron, OreCopper, Animal, BerryBush
};

struct ResourceInstance {
    ResourceType type;
    int x; // grid coord
    int y;
};

struct WorldParams {
    int width = 512;
    int height = 512;
    uint32_t seed = 1337u;

    // 0..1 thresholds
    float seaLevel = 0.45f;
    float beachWidth = 0.02f;

    // Terrain shaping
    bool   archipelago = false; // if true, apply island falloff
    float  worldWarp = 30.0f;   // domain-warp amplitude in cells
    int    warpOctaves = 2;

    // Fractal noise
    int    octaves = 6;
    float  lacunarity = 2.0f;
    float  gain = 0.5f;
    float  freq = 1.0f / 256.0f;

    // Erosion
    bool   applyErosion = true;
    int    erosionDroplets = 50000;
    int    erosionMaxSteps = 30;

    // Biome fields
    float  tempPolarBias = 0.35f; // cooler at top/bottom (0..1)
    float  lapseRate = 0.6f;      // cooling with altitude 0..1
    float  moistureFreq = 1.0f / 256.0f;

    // Resources
    bool   placeResources = true;
};

struct WorldData {
    int w=0, h=0;
    std::vector<float> height;      // [0..1]
    std::vector<float> moisture;    // [0..1]
    std::vector<float> temperature; // [0..1]
    std::vector<Biome> biome;       // same size as height
    std::vector<ResourceInstance> resources;
};

// Utility to colorize biomes for previews (RGBA8).
std::vector<uint8_t> makeBiomePreviewRGBA(const WorldData&);

} // namespace procgen
