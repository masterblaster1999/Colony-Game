#include "DpiAware.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winplat {
bool EnablePerMonitorV2DpiAwareness() {
    // Prefer manifest (PMv2) but also try programmatic opt-in for safety
    // Note: Microsoft recommends manifest, programmatic is ok if done very early.
    // Docs: "Setting the default DPI awareness for a process"
    // and SetProcessDpiAwarenessContext.
    // https://learn.microsoft.com/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext
    auto pSet = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (pSet) {
        if (pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return true;
        }
    }
    return false;
}
} // namespace winplat
