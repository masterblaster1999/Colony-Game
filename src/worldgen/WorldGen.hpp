#pragma once

#include <vector>
#include <memory>
#include <cstdint>

// Keep this header self-sufficient after the Stages refactor:
//  - WorldGenFwd: forward declarations + StagePtr
//  - StagesTypes: StageId + IWorldGenStage interface
//  - StageContext: StageContext & GeneratorSettings definitions
#include "WorldGenFwd.hpp"
#include "StagesTypes.hpp"
#include "StageContext.hpp"

namespace colony::worldgen {

class WorldGenerator {
public:
    explicit WorldGenerator(GeneratorSettings settings);

    // Register/override stages (call before generating any chunks)
    void clearStages();
    void addStage(StagePtr stage); // appended in order

    // Synchronous generation (pure & deterministic)
    [[nodiscard]] WorldChunk generate(ChunkCoord coord) const;

    // Convenience: generate with a temporary world-seed override
    [[nodiscard]] WorldChunk generate(ChunkCoord coord, std::uint64_t altWorldSeed) const;

    // Access settings
    [[nodiscard]] const GeneratorSettings& settings() const noexcept { return settings_; }

private:
    [[nodiscard]] WorldChunk makeEmptyChunk_(ChunkCoord coord) const;
    void run_(WorldChunk& chunk, std::uint64_t worldSeed) const;

private:
    GeneratorSettings settings_;
    std::vector<StagePtr> stages_;
};

// ----- Default stages (simple but functional) -----
// These are provided out of the box; you can remove or replace them.

class BaseElevationStage final : public IWorldGenStage {
public:
    StageId id() const noexcept override { return StageId::BaseElevation; }
    const char* name() const noexcept override { return "BaseElevation"; }
    void generate(StageContext& ctx) override;
};

class ClimateStage final : public IWorldGenStage {
public:
    StageId id() const noexcept override { return StageId::Climate; }
    const char* name() const noexcept override { return "Climate"; }
    void generate(StageContext& ctx) override;
};

class HydrologyStage final : public IWorldGenStage {
public:
    StageId id() const noexcept override { return StageId::Hydrology; }
    const char* name() const noexcept override { return "Hydrology"; }
    void generate(StageContext& ctx) override;
};

class BiomeStage final : public IWorldGenStage {
public:
    StageId id() const noexcept override { return StageId::Biome; }
    const char* name() const noexcept override { return "Biome"; }
    void generate(StageContext& ctx) override;
};

class ScatterStage final : public IWorldGenStage {
public:
    StageId id() const noexcept override { return StageId::Scatter; }
    const char* name() const noexcept override { return "Scatter"; }
    void generate(StageContext& ctx) override;
};

} // namespace colony::worldgen
