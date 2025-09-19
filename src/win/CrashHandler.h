#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Installs a top-level exception filter that writes a minidump (*.dmp)
// to %LOCALAPPDATA%\ColonyGame\crashdumps (or to dumpDir if provided).
// appDisplayName is used in filenames and messages.
bool InstallCrashHandler(const wchar_t* appDisplayName,
                         const wchar_t* dumpDir = nullptr);
