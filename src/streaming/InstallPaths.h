#pragma once

#include <filesystem>

// IMPORTANT:
// Declare only (no definition in this header) to avoid ODR/linker multiple-definition issues.
// Provide the single definition in src/platform/win/WinUtils.cpp (or equivalent).
std::filesystem::path GetExecutableDir();

// Base content directory placed next to the EXE.
[[nodiscard]] inline std::filesystem::path GetContentDir()
{
    const auto exeDir = GetExecutableDir();
    if (exeDir.empty())
        return {}; // caller can handle missing/unknown exe dir
    return exeDir / L"Content";
}

struct TerrainStreamingDirs
{
    std::filesystem::path root;    // .../Content/Streaming/Terrain
    std::filesystem::path height;  // .../Content/Streaming/Terrain/Height
    std::filesystem::path albedo;  // .../Content/Streaming/Terrain/Albedo
    std::filesystem::path normal;  // .../Content/Streaming/Terrain/Normal
};

[[nodiscard]] inline TerrainStreamingDirs GetTerrainDirs()
{
    const auto streaming = GetContentDir() / L"Streaming";
    const auto terrain   = streaming / L"Terrain";
    return { terrain, terrain / L"Height", terrain / L"Albedo", terrain / L"Normal" };
}
