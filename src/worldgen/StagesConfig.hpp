#pragma once

#include <string>
#include <filesystem>
#include <cstdint>
#include "StagesTypes.hpp"   // StageParams, StageContext

// If your repo has cg::ClimateParams / cg::HydroParams (src/worldgen/Hydrology.hpp)
#if __has_include("worldgen/Hydrology.hpp")
  #include "worldgen/Hydrology.hpp"
  #define CG_HAS_HYDROLOGY 1
#else
  #define CG_HAS_HYDROLOGY 0
#endif

namespace colony::worldgen {

// Optional noise controls (matches [noise] section)
struct NoiseParams {
    int   fbmOctaves         = 5;
    float fbmGain            = 0.5f;
    float fbmLacunarity      = 2.0f;
    float domainWarpStrength = 0.75f;
};

// Debug toggles (matches [debug] section)
struct DebugParams {
    bool drawTileGrid     = false;
    bool exportDebugMaps  = false;
    int  seed             = 42;   // -1 => randomize elsewhere
};

// Backward‑compat shim: keep cfg.stage.params.* working without pulling in a heavy StageContext.
struct StageTuning {
    StageParams params{};
};

// Aggregate runtime config loaded from INI.
struct StagesRuntimeConfig {
    // Contains StageParams (tile size, MU scale, grid dims) via a lightweight wrapper.
    StageTuning stage{};
#if CG_HAS_HYDROLOGY
    cg::ClimateParams climate{};
    // Name matches .cpp usage (cfg.hydrology.*) to avoid C2039.
    cg::HydroParams   hydrology{};
#endif
    NoiseParams noise{};
    DebugParams debug{};
};

class StagesConfig {
public:
    // Load or throw std::runtime_error on unrecoverable failure (I/O).
    static StagesRuntimeConfig load(const std::wstring& iniPath);

    // Load but never throw; returns false if the file could not be read.
    static bool try_load(const std::wstring& iniPath, StagesRuntimeConfig& out);

    // Compute default path: <exe_dir>/assets/config/stages.ini
    static std::wstring default_path();

private:
    // Helpers
    static std::wstring exe_dir();
};

} // namespace colony::worldgen
