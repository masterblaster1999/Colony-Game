// src/worldgen/StagesApi.hpp
#pragma once
#include <cstdint>
#include <memory>

namespace colony::worldgen {

// Keep this scoped enum stable; values are used for RNG stream selection, etc.
enum class StageId : std::uint32_t {
    BaseElevation = 1,
    Climate      = 2,
    Hydrology    = 3,
    Biome        = 4,
    Scatter      = 5
};

struct StageContext; // forward declare (definition in StageContext.hpp)

// Polymorphic interface for all worldgen stages.
struct IWorldGenStage {
    virtual ~IWorldGenStage() = default;

    virtual StageId     id()   const noexcept = 0;
    virtual const char* name() const noexcept = 0;
    virtual void        generate(StageContext& ctx) = 0;
};

// Owning pointer for stages.
using StagePtr = std::unique_ptr<IWorldGenStage>;

} // namespace colony::worldgen
