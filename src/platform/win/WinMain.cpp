// src/platform/win/WinMain.cpp

#include "platform/win/WinCommon.h"
#include "platform/win/LauncherLogSingletonWin.h" // LauncherLog(), WriteLog(), LogsDir()

// PROCESS_DPI_AWARENESS (Win8.1+). We load functions dynamically at runtime.
// NOTE: We intentionally do NOT link Shcore.lib here because we resolve shcore.dll at runtime.
#include <shellscalingapi.h>

#include "platform/win/WinFiles.h"
#include "platform/win/WinWindow.h"

#include "core/App.h"
#include "core/Log.h"
#include "core/Crash.h"
#include "core/Config.h"

#include <string>

using namespace platform::win;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#   define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)-3)
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#   define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

static void EnablePerMonitorV2DPI()
{
    // Best practice is to set DPI awareness via an application manifest.
    // This is a best-effort runtime fallback/override and MUST be called before creating any windows.

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
        auto setProcessCtx = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

        if (setProcessCtx)
        {
            // Prefer Per-Monitor V2.
            if (setProcessCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                return;

            // If already set (e.g., via manifest), Windows commonly returns ACCESS_DENIED.
            if (::GetLastError() == ERROR_ACCESS_DENIED)
                return;

            // Fallback to Per-Monitor V1.
            if (setProcessCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
                return;

            if (::GetLastError() == ERROR_ACCESS_DENIED)
                return;
        }

        // If process-level API isn't available, try a thread-level override.
        using SetThreadDpiAwarenessContextFn = HANDLE(WINAPI*)(HANDLE);
        auto setThreadCtx = reinterpret_cast<SetThreadDpiAwarenessContextFn>(
            ::GetProcAddress(user32, "SetThreadDpiAwarenessContext"));

        if (setThreadCtx)
        {
            (void)setThreadCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }

    // Windows 8.1 fallback: shcore!SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
    if (HMODULE shcore = ::LoadLibraryW(L"shcore.dll"))
    {
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
        auto setProcess = reinterpret_cast<SetProcessDpiAwarenessFn>(
            ::GetProcAddress(shcore, "SetProcessDpiAwareness"));

        if (setProcess)
        {
            (void)setProcess(PROCESS_PER_MONITOR_DPI_AWARE);
            ::FreeLibrary(shcore);
            return;
        }

        ::FreeLibrary(shcore);
    }

    // Vista+ fallback: system DPI aware
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
    {
        using SetProcessDPIAwareFn = BOOL(WINAPI*)();
        auto setAware = reinterpret_cast<SetProcessDPIAwareFn>(
            ::GetProcAddress(user32, "SetProcessDPIAware"));
        if (setAware)
            (void)setAware();
    }
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // Stable working directory and save paths
    FixWorkingDirectory();

    const auto saveDir = GetSaveDir();

    // IMPORTANT: use the unified launcher log dir (no per-TU custom log-dir logic)
    const auto logDir = LogsDir();

    // Startup log (opened once per process; safe even before core::LogInit)
    auto& bootLog = LauncherLog();
    WriteLog(bootLog, L"[WinMain] Starting Colony Game");
    WriteLog(bootLog, L"[WinMain] saveDir=" + saveDir.wstring());
    WriteLog(bootLog, L"[WinMain] logDir =" + logDir.wstring());

    // Game logging/crash handling (still uses your core system)
    core::LogInit(logDir);
    core::InstallCrashHandler(logDir);

    // DPI awareness (manifest preferred)
    EnablePerMonitorV2DPI();

    core::Config cfg;
    (void)core::LoadConfig(cfg, saveDir);

    WinWindow window;
    if (!window.Create(L"Colony Game", cfg.windowWidth, cfg.windowHeight))
    {
        LOG_CRITICAL("Failed to create window");
        WriteLog(bootLog, L"[WinMain] window.Create FAILED");
        core::LogShutdown();
        return -1;
    }

    unsigned cw = 0, ch = 0;
    window.GetClientSize(cw, ch);

    core::App app;
    if (!app.Initialize(window.GetHWND(), cw, ch))
    {
        LOG_CRITICAL("Failed to initialize App");
        WriteLog(bootLog, L"[WinMain] app.Initialize FAILED");
        core::LogShutdown();
        return -2;
    }

    // Main loop
    while (window.ProcessMessages())
    {
        app.TickFrame();
    }

    app.Shutdown();
    core::SaveConfig(cfg, saveDir);
    core::LogShutdown();

    WriteLog(bootLog, L"[WinMain] Clean exit");
    return 0;
}
