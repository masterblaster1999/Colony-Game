#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Call at the very top of WinMain:
void FixWorkingDirectoryToExe();
bool EnsureSingleInstance(const wchar_t* mutexName); // returns false if already running
void SetPerMonitorDpiAware();                        // best-effort (runtime check)
bool InitCrashHandler(const wchar_t* dumpSubdir = L"crashdumps");

// Utilities (optional)
std::wstring GetExeDir();
bool DirectoryExists(const std::wstring& path);
