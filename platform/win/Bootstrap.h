#pragma once
#include <filesystem>
#include <windows.h>

namespace cg {

    // Must be called before creating any windows.
    void ConfigureDPI();

    // Set CWD to the EXE dir, add DLL search path, return the EXE directory.
    std::filesystem::path SetCurrentDirToExe();

    // Ensure res folder exists; returns the resolved path or empty path if missing.
    std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir);

    // Optional: avoid running multiple copies.
    HANDLE CreateSingleInstanceMutex(const wchar_t* name = L"ColonyGame_SingleInstance");

    // Hints to pick high-performance GPU (NV/AMD) when present.
    void SelectHighPerformanceGPU();

}
