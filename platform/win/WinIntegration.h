// platform/win/WinIntegration.h
#pragma once

#ifdef _WIN32
#include <wchar.h>

// Keep WinLauncher.cpp calls working without touching that file.
void InstallCrashHandler(const wchar_t* dumpDir);
void TryEnablePerMonitorV2Dpi();
#endif
