// tests/test_paths.cpp
#ifdef _WIN32
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>   // _wdupenv_s
#include <windows.h>
#include <shlobj.h>  // SHGetKnownFolderPath

// Try to use the project's helpers if available.
#if __has_include("platform/win/PathUtilWin.h")
  #include "platform/win/PathUtilWin.h"
  #define COLONY_HAS_PROJECT_PATHUTIL 1
#else
  #define COLONY_HAS_PROJECT_PATHUTIL 0
#endif

namespace fs = std::filesystem;

static fs::path colony_saved_games_dir() {
#if COLONY_HAS_PROJECT_PATHUTIL
    // Expected to exist in your repo (namespace may be winpath or similar).
    return winpath::saved_games_dir();
#else
    // Fallback: KNOWNFOLDERID Saved Games.
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, NULL, &p))) {
        out = fs::path(p);
        CoTaskMemFree(p);
    } else {
        out = fs::temp_directory_path();
    }
    fs::create_directories(out);
    return out;
#endif
}

static fs::path colony_writable_data_dir() {
#if COLONY_HAS_PROJECT_PATHUTIL
    // Expected to exist in your repo.
    return winpath::writable_data_dir();
#else
    // Fallback: %LOCALAPPDATA%\ColonyGame
    wchar_t* env = nullptr; size_t len = 0;
    fs::path base = fs::temp_directory_path();
    if (_wdupenv_s(&env, &len, L"LOCALAPPDATA") == 0 && env) {
        base = fs::path(env);
        free(env);
    }
    fs::path out = base / L"ColonyGame";
    fs::create_directories(out);
    return out;
#endif
}

TEST_CASE("Saved Games directory exists and is writable") {
    const fs::path dir = colony_saved_games_dir();
    REQUIRE_MESSAGE(!dir.empty(), "Saved Games dir is empty path");
    REQUIRE_MESSAGE(fs::exists(dir), "Saved Games dir does not exist: " << dir.string());

    const fs::path probe = dir / L"doctest_savedgames_probe.tmp";
    {
        std::ofstream f(probe, std::ios::binary);
        REQUIRE_MESSAGE(f.good(), "Cannot create a file in Saved Games: " << dir.string());
        f << "ok";
    }
    CHECK(fs::file_size(probe) == 2);
    fs::remove(probe);
}

TEST_CASE("Writable data directory exists and is writable") {
    const fs::path dir = colony_writable_data_dir();
    fs::create_directories(dir);
    REQUIRE_MESSAGE(fs::exists(dir), "Writable data dir does not exist: " << dir.string());

    const fs::path probe = dir / L"doctest_writable_probe.tmp";
    {
        std::ofstream f(probe, std::ios::binary);
        REQUIRE_MESSAGE(f.good(), "Cannot create a file in writable data dir: " << dir.string());
        f << "ok";
    }
    CHECK(fs::file_size(probe) == 2);
    fs::remove(probe);
}
#else
// Non-Windows builds: make the test binary do nothing.
int main() { return 0; }
#endif
