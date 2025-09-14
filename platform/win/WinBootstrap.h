#pragma once
#include <string>
#include <filesystem>

namespace winboot {

struct Options {
    std::wstring mutexName     = L"ColonyGame-SingleInstance";
    std::wstring assetDirName  = L"res";   // name of your assets folder
    bool singleInstance        = true;
    bool showConsoleInDebug    = true;     // AllocConsole in Debug builds
    bool makeDpiAware          = true;     // Per-monitor v2 if available
    bool writeCrashDumps       = true;     // write .dmp files to logs/
};

// Call at the very start of WinMain / wWinMain / main.
void Preflight(const Options& = {});

// Optional: call before exit (releases mutex, closes log).
void Shutdown();

// Resolved game root (where assetDirName lives). Set during Preflight().
std::filesystem::path GameRoot();

} // namespace winboot
