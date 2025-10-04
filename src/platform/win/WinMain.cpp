#include <windows.h>
#include <shellscalingapi.h> // DPI awareness contexts (via GetProcAddress at runtime)
#pragma comment(lib, "Shcore.lib")

#include "platform/win/WinFiles.h"
#include "platform/win/WinWindow.h"
#include "core/App.h"
#include "core/Log.h"
#include "core/Crash.h"
#include "core/Config.h"

using namespace platform::win;

static void EnablePerMonitorV2DPI() {
    // SetThreadDpiAwarenessContext is available on Win10+; load dynamically.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32) return;
    using SetThreadDpiAwarenessContextFn = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetThreadDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    FreeLibrary(user32);
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Stable working directory and save paths
    FixWorkingDirectory();
    const auto saveDir = GetSaveDir();
    const auto logDir  = GetLogDir();

    // Logging and crash handling
    core::LogInit(logDir);
    core::InstallCrashHandler(logDir);

    // Per-Monitor v2 DPI awareness (crisper UI on multi-DPI setups)
    EnablePerMonitorV2DPI(); // Per‑window/thread DPI awareness via Win10 API. :contentReference[oaicite:5]{index=5}

    core::Config cfg;
    (void)core::LoadConfig(cfg, saveDir);

    WinWindow window;
    if (!window.Create(L"Colony Game", cfg.windowWidth, cfg.windowHeight)) {
        LOG_CRITICAL("Failed to create window");
        core::LogShutdown();
        return -1;
    }

    unsigned cw=0, ch=0;
    window.GetClientSize(cw, ch);

    core::App app;
    if (!app.Initialize(window.GetHWND(), cw, ch)) {
        LOG_CRITICAL("Failed to initialize App");
        core::LogShutdown();
        return -2;
    }

    // Main loop
    while (window.ProcessMessages()) {
        app.TickFrame();
        // Optional: Sleep(0) or timing control here
    }

    app.Shutdown();
    core::SaveConfig(cfg, saveDir);
    core::LogShutdown();
    return 0;
}
