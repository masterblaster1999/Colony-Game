#pragma once

#include <cstdint>
#include <string>

struct HydraulicErosionSettings
{
    std::uint32_t iterations    = 25000;
    float         rainRate      = 0.02f;
    float         evaporation   = 0.001f;
    float         sedimentCap   = 0.5f;
    float         timeStep      = 0.02f;
    float         gravity       = 9.81f;
    float         initialWater  = 0.0f;
};

struct ThermalErosionSettings
{
    std::uint32_t iterations   = 5000;
    float         talusAngle   = 0.6f;
    float         erosionRate  = 0.4f;
    float         minSlope     = 0.02f;
    float         timeStep     = 0.02f;
};

struct TerrainBaseSettings
{
    std::uint32_t seed        = 12345;
    std::uint32_t width       = 512;
    std::uint32_t height      = 512;
    float         baseHeight  = 0.0f;
};

struct TerrainErosionConfig
{
    TerrainBaseSettings     terrain;
    HydraulicErosionSettings hydraulic;
    ThermalErosionSettings   thermal;

    /// Load from a given path; falls back to defaults on error.
    static TerrainErosionConfig LoadFromFile(const std::string& path);

    /// Convenience: load from the standard install path:
    ///   <exe_dir>/assets/config/terrain_erosion.json
    static TerrainErosionConfig LoadDefault();

    /// Returns the default config path resolved relative to the exe directory.
    static std::string GetDefaultConfigPath();
};
