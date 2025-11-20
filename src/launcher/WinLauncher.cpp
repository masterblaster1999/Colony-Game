// src/launcher/WinLauncher.cpp

#include <windows.h>
#include "CrashDumpWin.h"
// plus whatever header declares your game app / main loop

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Mark currently-unused parameters to silence C4100 (unreferenced parameter)
    (void)hInstance;
    (void)nCmdShow;

    // This is the line your compiler is currently complaining about.
    // Turn it into a proper variable:
    CrashDumpGuard guard{L"Colony-Game"};

    // Optional, but nice: DPI awareness, etc.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // TODO: hook into your actual game/app entrypoint.
    // e.g.
    // ColonyGame::App app{hInstance};
    // if (!app.Initialize(nCmdShow)) return -1;
    // return app.Run();

    return 0;
}
