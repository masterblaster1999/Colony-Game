// platform/win/LauncherLogSingletonWin.h
//
// Header-only singleton wrapper around OpenLogFile() so that *all* startup code
// shares one process-wide log stream. This avoids "double-open" fights across
// WinMain / WinBootstrap / AppMain / WinLauncher.
//
// C++11 guarantees thread-safe initialization for function-local statics.

#pragma once

#include "platform/win/LauncherLoggingWin.h"

inline std::wofstream& LauncherLog()
{
    static std::wofstream s_log = OpenLogFile(); // opened exactly once per process
    return s_log;
}
