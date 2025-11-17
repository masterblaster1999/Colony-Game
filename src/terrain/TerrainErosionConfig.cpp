#include "terrain/TerrainErosionConfig.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp> // via vcpkg: nlohmann-json
#ifdef _WIN32
#   include <Windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    TerrainErosionConfig MakeDefaultConfig()
    {
        TerrainErosionConfig cfg;
        // All defaults already set in structs, but you can tweak here if needed
        return cfg;
    }

    template <typename T>
    T GetOr(const json& j, const char* key, const T& fallback)
    {
        if (!j.is_object())
            return fallback;
        auto it = j.find(key);
        if (it == j.end())
            return fallback;
        try
        {
            return it->get<T>();
        }
        catch (...)
        {
            return fallback;
        }
    }
}

TerrainErosionConfig TerrainErosionConfig::LoadFromFile(const std::string& path)
{
    TerrainErosionConfig cfg = MakeDefaultConfig();

    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f)
    {
        // Optional: hook into your logging system instead
        // fprintf(stderr, "TerrainErosionConfig: could not open '%s', using defaults\n", path.c_str());
        return cfg;
    }

    json root;
    try
    {
        f >> root;
    }
    catch (const std::exception&)
    {
        // fprintf(stderr, "TerrainErosionConfig: parse error in '%s', using defaults\n", path.c_str());
        return cfg;
    }

    // Terrain base
    const json terrain = root.value("terrain", json::object());
    cfg.terrain.seed       = GetOr<std::uint32_t>(terrain, "seed",       cfg.terrain.seed);
    cfg.terrain.width      = GetOr<std::uint32_t>(terrain, "width",      cfg.terrain.width);
    cfg.terrain.height     = GetOr<std::uint32_t>(terrain, "height",     cfg.terrain.height);
    cfg.terrain.baseHeight = GetOr<float>        (terrain, "base_height",cfg.terrain.baseHeight);

    // Hydraulic
    const json hydraulic   = root.value("hydraulic", json::object());
    cfg.hydraulic.iterations   = GetOr<std::uint32_t>(hydraulic, "iterations",       cfg.hydraulic.iterations);
    cfg.hydraulic.rainRate     = GetOr<float>        (hydraulic, "rain_rate",        cfg.hydraulic.rainRate);
    cfg.hydraulic.evaporation  = GetOr<float>        (hydraulic, "evaporation",      cfg.hydraulic.evaporation);
    cfg.hydraulic.sedimentCap  = GetOr<float>        (hydraulic, "sediment_capacity",cfg.hydraulic.sedimentCap);
    cfg.hydraulic.timeStep     = GetOr<float>        (hydraulic, "time_step",        cfg.hydraulic.timeStep);
    cfg.hydraulic.gravity      = GetOr<float>        (hydraulic, "gravity",          cfg.hydraulic.gravity);
    cfg.hydraulic.initialWater = GetOr<float>        (hydraulic, "initial_water",    cfg.hydraulic.initialWater);

    // Thermal
    const json thermal     = root.value("thermal", json::object());
    cfg.thermal.iterations  = GetOr<std::uint32_t>(thermal, "iterations",   cfg.thermal.iterations);
    cfg.thermal.talusAngle  = GetOr<float>        (thermal, "talus_angle",  cfg.thermal.talusAngle);
    cfg.thermal.erosionRate = GetOr<float>        (thermal, "erosion_rate", cfg.thermal.erosionRate);
    cfg.thermal.minSlope    = GetOr<float>        (thermal, "min_slope",    cfg.thermal.minSlope);
    cfg.thermal.timeStep    = GetOr<float>        (thermal, "time_step",    cfg.thermal.timeStep);

    return cfg;
}

TerrainErosionConfig TerrainErosionConfig::LoadDefault()
{
    return LoadFromFile(GetDefaultConfigPath());
}

std::string TerrainErosionConfig::GetDefaultConfigPath()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        // Fallback: rely on current working directory
        return "assets/config/terrain_erosion.json";
    }

    fs::path exePath = fs::path(std::string(buf, len)).parent_path();
    fs::path cfgPath = exePath / "assets" / "config" / "terrain_erosion.json";
    return cfgPath.string();
#else
    // You said Windows only, but keep a sane default for completeness
    return "assets/config/terrain_erosion.json";
#endif
}
