#pragma once
#ifdef _WIN32
#include <wchar.h>

// Keeps existing WinLauncher.cpp calls working:
void InstallCrashHandler(const wchar_t* dumpDir);
void TryEnablePerMonitorV2Dpi();
#endif
