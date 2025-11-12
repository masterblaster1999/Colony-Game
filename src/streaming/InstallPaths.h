#pragma once
#include <filesystem>
#include <string>
#include <array>
#define NOMINMAX
#include <Windows.h>

// Windows-only: resolve the absolute directory of the running EXE.
// See: GetModuleFileNameW docs. :contentReference[oaicite:3]{index=3}
inline std::filesystem::path GetExecutableDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        // Fallback: current path (shouldn't happen in normal runs)
        return std::filesystem::current_path();
    }
    std::filesystem::path p(buf);
    return p.remove_filename();
}

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
