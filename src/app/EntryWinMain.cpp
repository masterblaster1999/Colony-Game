#include <Windows.h>
#include "CrashHandler.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Install crash dumps immediately so even early-start failures are captured.
    InstallCrashHandler(L"ColonyGame");

    App app;
    return app.Run(hInstance);
}
