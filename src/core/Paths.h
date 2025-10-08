// src/core/Paths.h
#pragma once
#include <filesystem>
namespace paths {
    // Resolve exe dir and standard subfolders; wide-safe for Windows.
    std::filesystem::path exe_dir();
    void                set_working_dir_to_exe();

    // Project roots (auto-detect build tree vs. installed)
    const std::filesystem::path& root();      // repo root or install root
    std::filesystem::path assets();           // .../assets
    std::filesystem::path audio();            // .../audio
    std::filesystem::path config();           // .../assets/config (your current layout)
    std::filesystem::path shaders_dir();      // prefer renderer/Shaders if present, else /shaders
}
