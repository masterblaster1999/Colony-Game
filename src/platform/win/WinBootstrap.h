// src/platform/win/WinBootstrap.h
#pragma once
#include <string>
#include <filesystem>

namespace winboot {

// Configuration used by WinBootstrap.cpp
struct Options {
    bool makeDpiAware        = true;
    bool writeCrashDumps     = true;
    bool singleInstance      = true;
    bool showConsoleInDebug  = false;

    // Paths / names
    std::wstring assetDirName = L"assets";
    std::wstring mutexName    = L"ColonyGameMutex";
};

// API
std::filesystem::path GameRoot();
void Preflight(const Options& opt);
void Shutdown();

} // namespace winboot
