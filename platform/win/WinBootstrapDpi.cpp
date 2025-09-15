// Lightweight, header-free dynamic loading so you don't need extra SDK includes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace win {

// From Shcore.h (avoid including it just for this).
enum PROCESS_DPI_AWARENESS { PROCESS_DPI_UNAWARE = 0, PROCESS_SYSTEM_DPI_AWARE = 1, PROCESS_PER_MONITOR_DPI_AWARE = 2 };

// In winuser.h; define locally if older SDK.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
using DPI_AWARENESS_CONTEXT = HANDLE;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

void EnablePerMonitorDpiAwareness() {
    // Preferred: Per-Monitor V2 via SetProcessDpiAwarenessContext (Win10+)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetProcCtxFn = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
        if (auto setCtx = reinterpret_cast<SetProcCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return; // Success
            }
        }
    }
    // Fallback: Per-Monitor (Win8.1+)
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        using SetProcAwarenessFn = HRESULT (WINAPI *)(PROCESS_DPI_AWARENESS);
        if (auto setAw = reinterpret_cast<SetProcAwarenessFn>(GetProcAddress(shcore, "SetProcessDpiAwareness"))) {
            setAw(PROCESS_PER_MONITOR_DPI_AWARE);
        }
        FreeLibrary(shcore);
    } else {
        // Legacy fallback: system DPI aware (Vista+)
        using SetDPIAwareFn = BOOL (WINAPI *)(void);
        if (auto setAware = reinterpret_cast<SetDPIAwareFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"))) {
            setAware();
        }
    }
}

} // namespace win
