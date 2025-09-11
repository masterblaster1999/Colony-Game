#pragma once
#include <filesystem>
#include <cstdlib>
#include <string_view>

#if __has_include(<version>)
  #include <version>
#endif

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

namespace cg {

// Directory containing the running executable (robust & cross-platform)
inline std::filesystem::path exe_dir() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(),
                                   static_cast<DWORD>(buf.size()));
    if (len == 0) return std::filesystem::current_path();
    buf.resize(len);
    return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string tmp(size, '\0');
    if (_NSGetExecutablePath(tmp.data(), &size) != 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(tmp)).parent_path();
#else
    // Linux/Unix that expose /proc
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return std::filesystem::current_path();
#endif
}

// Resolve the root directory for data/assets in multiple robust ways.
inline std::filesystem::path data_root() {
    // 0) Environment override (useful for tests and Steam/itch launchers)
    if (const char* p = std::getenv("COLONY_GAME_DATA_DIR"); p && *p) {
        return std::filesystem::weakly_canonical(std::filesystem::path(p));
    }

    const auto exe = exe_dir();

    // 1) Installed layout: <exe>/../share/ColonyGame/assets
    auto candidate = exe / ".." / "share" / "ColonyGame" / "assets";
    if (std::filesystem::exists(candidate)) {
        return std::filesystem::weakly_canonical(candidate);
    }

    // 2) Dev layout: <repo_or_build>/assets next to the exe or cwd
    candidate = exe / "assets";
    if (std::filesystem::exists(candidate)) {
        return std::filesystem::weakly_canonical(candidate);
    }

    candidate = std::filesystem::current_path() / "assets";
    if (std::filesystem::exists(candidate)) {
        return std::filesystem::weakly_canonical(candidate);
    }

#ifdef COLONY_INSTALLED_ASSETS_DIR
    // 3) Compile-time configured absolute fallback from CMake (see below)
    candidate = std::filesystem::path(COLONY_INSTALLED_ASSETS_DIR);
    if (std::filesystem::exists(candidate)) {
        return std::filesystem::weakly_canonical(candidate);
    }
#endif

    // 4) Last-resort: current dir (so game still "runs" with clear logging)
    return std::filesystem::current_path();
}

// Helper: build full path to an asset (e.g., asset("textures/ui.png"))
inline std::filesystem::path asset(std::string_view rel) {
    return data_root() / rel;
}

} // namespace cg
