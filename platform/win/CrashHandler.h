#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>

namespace winplat {

struct CrashConfig {
    std::wstring appName = L"ColonyGame";
    // Where to write .dmp files; if empty, defaults to %LOCALAPPDATA%\ColonyGame\crashdumps
    std::filesystem::path dumpDir{};
    bool showMessageBox = true; // show a simple message box after writing the dump
};

// Installs a top‑level exception filter and several CRT handlers to ensure
// minidumps are written on unhandled exceptions or fatal conditions.
void InstallCrashHandler(const CrashConfig& cfg);

// Create a dump immediately (can be called from your code paths if desired).
bool WriteMiniDump(const std::filesystem::path& dumpPath, EXCEPTION_POINTERS* exceptionPointers);

// Returns the default crash dump directory: %LOCALAPPDATA%\<appName>\crashdumps
std::filesystem::path GetDefaultCrashDumpDir(const std::wstring& appName);

} // namespace winplat
