#pragma once
#ifdef _WIN32
#include <wchar.h>

// Keeps existing WinLauncher.cpp calls working:
void InstallCrashHandler();                       // NEW: zero-arg overload
void InstallCrashHandler(const wchar_t* dumpDir); // existing
void TryEnablePerMonitorV2Dpi();
#endif
