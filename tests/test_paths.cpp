// tests/test_paths.cpp
//
// NOTE:
//   Do NOT define DOCTEST_CONFIG_IMPLEMENT* in this file.
//   tests/test_main.cpp is the only TU that provides doctest implementation + main.

#include <doctest/doctest.h>

#include <cstdlib>     // std::getenv
#include <filesystem>  // std::filesystem
#include <fstream>     // std::ifstream
#include <string>
#include <iostream>

namespace fs = std::filesystem;

namespace {

fs::path get_project_root() {
    // Try env var first (useful in CI or when running from arbitrary working dirs).
    if (const char* env = std::getenv("COLONY_PROJECT_ROOT")) {
        if (*env)
            return fs::path(env);
    }

    // Fallback: walk up from current working directory to find repo markers.
    fs::path cwd = fs::current_path();
    for (int i = 0; i < 10; ++i) {
        if (fs::exists(cwd / "CMakeLists.txt") && fs::exists(cwd / "README.md")) {
            return cwd;
        }
        fs::path parent = cwd.parent_path();
        if (parent == cwd)
            break;
        cwd = parent;
    }

    // Worst-case fallback.
    return fs::current_path();
}

fs::path assets_dir() {
    return get_project_root() / "assets";
}

} // namespace

TEST_CASE("assets directory exists") {
    const fs::path dir = assets_dir();
    INFO("assets dir: ", dir.string());

    CHECK_MESSAGE(fs::exists(dir), "assets directory is missing");
    CHECK_MESSAGE(fs::is_directory(dir), "assets path is not a directory");
}

TEST_CASE("can open a known asset file if present") {
    const fs::path dir = assets_dir();
    const fs::path candidate = dir / "placeholder.txt";
    INFO("candidate: ", candidate.string());

    if (!fs::exists(candidate)) {
        WARN("candidate asset not present; skipping");
        return;
    }

    std::ifstream f(candidate, std::ios::binary);
    CHECK_MESSAGE(f.good(), "could not open candidate asset file");
}

TEST_CASE("filesystem path conversions are safe on Windows") {
    const fs::path p = assets_dir();
    CHECK_NOTHROW(p.wstring());
    CHECK_NOTHROW(p.u8string());
}
