#include "Dpi.h"
#include <windows.h>
#include <shellscalingapi.h>
#pragma comment(lib, "Shcore.lib")

namespace winplat {
void EnableDpiAwareness() {
    // Microsoft recommends manifest for default; this is a runtime fallback to PerMonitorV2. :contentReference[oaicite:9]{index=9}
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto setCtx = reinterpret_cast<SetCtxFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        using SetAwFn = HRESULT (WINAPI*)(PROCESS_DPI_AWARENESS);
        if (auto setAw = reinterpret_cast<SetAwFn>(GetProcAddress(shcore, "SetProcessDpiAwareness"))) {
            setAw(PROCESS_PER_MONITOR_DPI_AWARE); // older Windows 8.1+
        }
        FreeLibrary(shcore);
        return;
    }
    SetProcessDPIAware(); // Vista fallback. See DPI docs for ordering. :contentReference[oaicite:10]{index=10}
}
}
