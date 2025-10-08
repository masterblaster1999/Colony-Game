// src/core/Paths.cpp
#include "Paths.h"
#include <Windows.h>

using std::filesystem::path;

namespace {
    path detect_root(const path& exeDir) {
        // If running from build tree: .../bin/Debug/YourExe.exe -> climb until we find assets/
        path p = exeDir;
        for (int i = 0; i < 4; ++i) {
            if (exists(p / "assets")) return p;
            p = p.parent_path();
        }
        // Installed layout: assets reside next to exe
        if (exists(exeDir / "assets")) return exeDir;
        return exeDir; // best effort
    }
}

namespace paths {
    path exe_dir() {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        path p(buf, buf + (n ? wcslen(buf) : 0));
        return p.parent_path();
    }

    void set_working_dir_to_exe() {
        const auto dir = exe_dir();
        SetCurrentDirectoryW(dir.c_str());
    }

    const path& root() {
        static path r = detect_root(exe_dir());
        return r;
    }

    path assets()      { return root() / L"assets"; }
    path audio()       { return root() / L"audio"; }
    path config()      { return assets() / L"config"; }
    path shaders_dir() {
        // Prefer renderer/Shaders if it exists, else top-level shaders
        const auto a = root() / L"renderer" / L"Shaders";
        if (exists(a)) return a;
        return root() / L"shaders";
    }
}
