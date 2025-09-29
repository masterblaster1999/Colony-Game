#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "AppWindow.h"
#include "CrashDump.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // DPI fallback (manifest is preferred)
    // Must be before any HWND creation
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    InstallCrashHandler(L"ColonyGame");

    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, 1280, 720))
        return -1;

    return app.MessageLoop();
}
