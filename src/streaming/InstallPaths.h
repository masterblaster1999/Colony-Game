#pragma once

#include <filesystem>
#include <string>
#include <array>

#include "platform/win/WinUtils.h" // provides GetExecutableDir()

// Base content directory placed next to the EXE.
inline std::filesystem::path GetContentDir()
{
    return GetExecutableDir() / L"Content";
}

struct TerrainStreamingDirs {
    std::filesystem::path root;    // .../Content/Streaming/Terrain
    std::filesystem::path height;  // .../Content/Streaming/Terrain/Height
    std::filesystem::path albedo;  // .../Content/Streaming/Terrain/Albedo
    std::filesystem::path normal;  // .../Content/Streaming/Terrain/Normal
};

inline TerrainStreamingDirs GetTerrainDirs()
{
    const auto streaming = GetContentDir() / L"Streaming";
    const auto terrain   = streaming / L"Terrain";
    return { terrain, terrain / L"Height", terrain / L"Albedo", terrain / L"Normal" };
}
