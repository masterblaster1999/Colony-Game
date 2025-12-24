// src/launcher/CrashDump.h
#pragma once

// NOTE:
// This header previously contained a git patch diff (accidentally committed).
// It is now a small compatibility wrapper for launcher-side crash dump setup.
//
// For the actual implementation, see CrashDumpWin.h/.cpp in this folder, or the
// platform/win crash facilities (CrashHandlerWin / CrashDumpWin).

#include "CrashDumpWin.h"

namespace wincrash {

// Installs an unhandled exception filter for the current process.
// This wrapper keeps the guard alive for process lifetime.
inline void InitCrashHandler(const wchar_t* appName = L"Colony-Game") noexcept
{
    static CrashDumpGuard guard{ appName ? appName : L"Colony-Game" };
    (void)guard;
}

} // namespace wincrash

// Back-compat alias used by some older code.
inline void InstallCrashHandler(const wchar_t* appName = L"Colony-Game") noexcept
{
    wincrash::InitCrashHandler(appName);
}
