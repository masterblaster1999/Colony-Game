#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace winboot {
    struct Options {
        // Keep this minimal and only include fields you actually access in WinBootstrap.cpp.
        // Examples below are conservative placeholders:
        std::optional<std::filesystem::path> content_root;
        std::wstring assets_subdir = L"assets";
        bool single_instance = true;
        bool attach_console = false;
        bool borderless_fullscreen = false;
        int width = 1280;
        int height = 720;
        bool vsync = true;
    };
}
