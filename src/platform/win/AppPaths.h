// AppPaths.h
#pragma once
#include <filesystem>
namespace app {
    inline std::filesystem::path exe_dir();
    inline std::filesystem::path asset_root();
}

// AppPaths.cpp
#include "AppPaths.h"
#include <windows.h>
#include <vector>

namespace fs = std::filesystem;

namespace app {
    fs::path exe_dir() {
        wchar_t buf[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path p = fs::path(std::wstring(buf, len)).parent_path();
        return fs::weakly_canonical(p);
    }
    fs::path asset_root() {
        // Prefer colocated "assets" beside executable, fallback to relative dev paths:
        const fs::path exe = exe_dir();
        for (auto cand : { exe / L"assets", exe.parent_path() / L"assets" }) {
            if (fs::exists(cand)) return cand;
        }
        // Last resort: project layout variants:
        for (auto cand : { exe / L"..\\..\\assets", exe / L"..\\assets" }) {
            if (fs::exists(cand)) return fs::weakly_canonical(cand);
        }
        return exe; // still returns something valid
    }
}
