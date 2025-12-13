// src/AppMain.cpp
//
// Centralized Windows header policy: defines WIN32_LEAN_AND_MEAN, NOMINMAX, STRICT
// and includes <windows.h> safely once for the entire project.

#include "platform/win/WinCommon.h"

#include "platform/win/LauncherLogSingletonWin.h" // LauncherLog(), WriteLog()

#include "AppWindow.h"
#include "CrashDump.h"

#include <string>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#   define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

namespace
{
    void HardenDllSearch()
    {
        // Windows 8+ / KB2533623: restrict default DLL search path to safe locations.
        // If not present (older systems), this call will simply fail and we ignore it.
        using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);

        if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
        {
            auto pSetDefaultDllDirectories =
                reinterpret_cast<SetDefaultDllDirectories_t>(
                    ::GetProcAddress(k32, "SetDefaultDllDirectories"));

            if (pSetDefaultDllDirectories)
            {
                // 0x00001000 == LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                (void)pSetDefaultDllDirectories(0x00001000);
            }
        }
    }

    void ApplyDpiAwareness()
    {
        // Microsoft recommends declaring PMv2 DPI in the app manifest.
        // This runtime path is a safe fallback as long as it runs BEFORE any HWND creation.
        // Try Per-Monitor V2 first (Win10+), then fall back to system DPI aware (Vista+).

        if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
        {
            using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);

            auto pSetProcessDpiAwarenessContext =
                reinterpret_cast<SetProcessDpiAwarenessContext_t>(
                    ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

            if (pSetProcessDpiAwarenessContext)
            {
                if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                    return; // success
            }
        }

        // Fallback: system DPI aware (Vista+). Preferred solution is a manifest.
        ::SetProcessDPIAware();
    }

    void NameMainThread()
    {
        // Give the main thread a descriptive name for VS/WinDbg/WPA.
        using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);

        if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
        {
            auto pSetThreadDescription =
                reinterpret_cast<SetThreadDescription_t>(
                    ::GetProcAddress(k32, "SetThreadDescription"));

            if (pSetThreadDescription)
                (void)pSetThreadDescription(::GetCurrentThread(), L"Main");
        }
    }
} // anonymous namespace

// Renamed from wWinMain to a normal function so that EntryWinMain.cpp
// can be the sole Windows entry point and delegate to this function.
int GameMain(HINSTANCE hInstance, PWSTR cmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(cmdLine);

    auto& log = LauncherLog();
    WriteLog(log, L"[AppMain] GameMain starting.");

    HardenDllSearch();
    ApplyDpiAwareness();
    InstallCrashHandler(L"ColonyGame");
    NameMainThread();

    WriteLog(log, L"[AppMain] Creating AppWindow...");

    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, 1280, 720))
    {
        WriteLog(log, L"[AppMain] AppWindow.Create FAILED");
        return -1;
    }

    const int exitCode = app.MessageLoop();
    WriteLog(log, L"[AppMain] MessageLoop exited code=" + std::to_wstring(exitCode));
    return exitCode;
}
