#pragma once
// Windows-only crash dump initialization (minidump on unhandled exception)
#include <windows.h>

namespace wincrash {
    // Initializes unhandled-exception filter and dump path. Safe to call once at startup.
    void InitCrashHandler(const wchar_t* appName);
}

// Convenience alias, in case other code calls this name:
inline void InstallCrashHandler(const wchar_t* appName) { wincrash::InitCrashHandler(appName); }
