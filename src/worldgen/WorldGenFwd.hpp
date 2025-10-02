#pragma once
#include <memory>
namespace colony::worldgen {
    struct GeneratorSettings;  // actually defined in StageContext.hpp
    struct StageContext;       // defined in StageContext.hpp
    struct WorldChunk;         // forward-declare if used by interface
    struct IWorldGenStage;     // defined where your stage interface lives
    using StagePtr = std::unique_ptr<IWorldGenStage>;
} // namespace colony::worldgen
