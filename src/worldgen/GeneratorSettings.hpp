// src/worldgen/GeneratorSettings.hpp
#pragma once
#include <cstdint>

namespace colony::worldgen {

// Keep this header tiny and stable: it's included widely.
struct GeneratorSettings {
    std::uint64_t worldSeed      = 0;   // used by WorldGenerator::generate(...) paths
    int           cellsPerChunk  = 64;  // used to size grids in makeEmptyChunk_()
    bool          enableHydrology = true;
    bool          enableScatter   = true;
    // Add more knobs as needed; keep POD for cheap copies.
};

} // namespace colony::worldgen
