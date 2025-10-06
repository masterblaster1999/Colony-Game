#pragma once

// Installs the crash handler for the running process.
// On Windows this wires up the CrashDumpWin facility (minidumps in %LOCALAPPDATA%\{app}\Crashes).
// Call once very early at startup (before creating threads/windows).
//
// Default name "ColonyGame" is used for the crash folder and file prefix.
#ifdef _WIN32
void InstallCrashHandler(const wchar_t* appName = L"ColonyGame");
#else
inline void InstallCrashHandler(const wchar_t* /*appName*/ = L"ColonyGame") {}
#endif
