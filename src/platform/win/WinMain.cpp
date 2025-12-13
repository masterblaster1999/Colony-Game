#include <windows.h>
#include <shellscalingapi.h> // PROCESS_DPI_AWARENESS (Win8.1+). We load functions dynamically at runtime.

// NOTE: We intentionally do NOT link Shcore.lib here because we resolve shcore.dll at runtime.

#include "platform/win/WinFiles.h"
#include "platform/win/WinWindow.h"
#include "core/App.h"
#include "core/Log.h"
#include "core/Crash.h"
#include "core/Config.h"

using namespace platform::win;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#    define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)-3)
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#    define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

static void EnablePerMonitorV2DPI()
{
    // Best practice is to set DPI awareness via an application manifest.
    // This is a best-effort runtime fallback/override and MUST be called before creating any windows.

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
        auto setProcessCtx = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

        if (setProcessCtx)
        {
            // Prefer Per-Monitor V2.
            if (setProcessCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                return;

            // If the DPI mode is already set (e.g., via manifest), Windows commonly returns ACCESS_DENIED.
            const DWORD err = ::GetLastError();
            if (err == ERROR_ACCESS_DENIED)
                return;

            // Fallback to Per-Monitor V1.
            if (setProcessCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
                return;

            if (::GetLastError() == ERROR_ACCESS_DENIED)
                return;
        }

        // If process-level API isn't available, a thread-level override can still help on some systems.
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
    if (user32)
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
    const auto logDir  = GetLogDir();

    // Logging and crash handling
    core::LogInit(logDir);
    core::InstallCrashHandler(logDir);

    // Per-Monitor v2 DPI awareness (crisper UI on multi-DPI setups)
    EnablePerMonitorV2DPI(); // (Removed invalid :contentReference[...] artifact)

    core::Config cfg;
    (void)core::LoadConfig(cfg, saveDir);

    WinWindow window;
    if (!window.Create(L"Colony Game", cfg.windowWidth, cfg.windowHeight))
    {
        LOG_CRITICAL("Failed to create window");
        core::LogShutdown();
        return -1;
    }

    unsigned cw = 0, ch = 0;
    window.GetClientSize(cw, ch);

    core::App app;
    if (!app.Initialize(window.GetHWND(), cw, ch))
    {
        LOG_CRITICAL("Failed to initialize App");
        core::LogShutdown();
        return -2;
    }

    // Main loop
    while (window.ProcessMessages())
    {
        app.TickFrame();
        // Optional: Sleep(0) or timing control here
    }

    app.Shutdown();
    core::SaveConfig(cfg, saveDir);
    core::LogShutdown();
    return 0;
}
