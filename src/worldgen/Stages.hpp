#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "Fields.hpp"
#include "RNG.hpp"

namespace colony::worldgen {

struct ChunkCoord { std::int32_t x = 0, y = 0; };
inline bool operator==(const ChunkCoord& a, const ChunkCoord& b) { return a.x==b.x && a.y==b.y; }

struct ObjectInstance {
    float wx = 0.f, wy = 0.f; // world-space (chunk-local) position
    std::uint32_t kind = 0;   // e.g., vegetation/rock type id
    float scale = 1.f, rot = 0.f;
};

enum class StageId : std::uint32_t {
    BaseElevation = 1,
    Climate       = 2,
    Hydrology     = 3,
    Biome         = 4,
    Scatter       = 5,
    // You can add more: Settlements=6, Roads=7, etc.
};

struct WorldChunk {
    ChunkCoord coord{};
    // Core fields (size set by settings)
    Grid<float>     height;     // meters
    Grid<float>     temperature;// Celsius
    Grid<float>     moisture;   // 0..1
    Grid<float>     flow;       // river flow accumulation
    Grid<std::uint8_t> biome;   // biome id
    std::vector<ObjectInstance> objects;
};

struct GeneratorSettings {
    std::uint64_t worldSeed = 0xC01_0NYULL;
    int cellsPerChunk = 128;      // resolution
    float cellSizeMeters = 1.0f;  // cell spacing
    bool enableHydrology = true;
    bool enableScatter   = true;
};

struct StageContext {
    const GeneratorSettings& settings;
    const ChunkCoord chunk;
    Pcg32& rng;
    WorldChunk& out; // read/write access to fields
};

// Base interface for pipeline stages
class IWorldGenStage {
public:
    virtual ~IWorldGenStage() = default;
    virtual StageId id() const noexcept = 0;
    virtual const char* name() const noexcept = 0;
    virtual void generate(StageContext& ctx) = 0;
};

using StagePtr = std::unique_ptr<IWorldGenStage>;

} // namespace colony::worldgen
