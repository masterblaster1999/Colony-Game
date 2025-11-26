#include <windows.h>
#include "platform/win/CrashHandler.h"
#include "platform/win/WindowsHost.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    InstallCrashHandler();

    // Prefer manifest; do a best‑effort runtime enable as fallback:
    TryEnablePerMonitorV2Dpi();  // safe if manifest already set. 

    WindowsHost host;
    if (!host.Create(hInst, nCmdShow)) return -1;

    // (Optional) initialize renderer here:
    // EnableDRED();  // D3D12 only, before device creation. 
    // CreateD3D12DeviceWithFallback(...); or CreateD3D11DeviceWithFallback(...);

    return host.MessageLoop();
}
