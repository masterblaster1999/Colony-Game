#include "platform/win/Paths.h"
#include "platform/win/SingleInstance.h"
// CrashHandler.h if you keep the .h name; or adjust include to your .hpp
#include "src/win/CrashHandler.h" // matches your commit location

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    winenv::init_process_environment(L"Colony-Game");
    InstallCrashHandler(L"Colony-Game"); // writes *.dmp into crashdumps/

    SingleInstanceGuard guard(L"Global\\ColonyGame_Singleton");
    if (guard.already_running()) {
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game", MB_OK|MB_ICONINFORMATION);
        return 0;
    }

    // ...create window, init D3D11Device, run loop...
}
