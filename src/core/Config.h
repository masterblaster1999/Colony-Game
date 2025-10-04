#pragma once
#include <filesystem>
#include <string>

namespace core {

struct Config {
    int   windowWidth  = 1280;
    int   windowHeight = 720;
    bool  vsync        = true;
};

bool LoadConfig(Config& cfg, const std::filesystem::path& saveDir);
bool SaveConfig(const Config& cfg, const std::filesystem::path& saveDir);

} // namespace core
