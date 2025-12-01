// platform/win/WinIntegration.cpp
// Windows-only shims to preserve legacy call sites in WinLauncher.cpp

#include "WinIntegration.h"
#include "CrashHandler.h"      // your existing crash handler API
#include <windows.h>           // do NOT define WIN32_LEAN_AND_MEAN here

void InstallCrashHandler(const wchar_t* dumpDir)
{
    // Forward to the current API; keep legacy call site working.
    CrashHandler::Install(dumpDir ? dumpDir : L".");
}

// Try to enable Per-Monitor (V2) DPI awareness as early as possible.
// Microsoft recommends manifest-based DPI awareness; this API is a supported fallback.
// Must be called BEFORE any window is created.
void TryEnablePerMonitorV2Dpi()
{
    HMODULE user = ::GetModuleHandleW(L"user32.dll");
    using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (user) {
        auto pSet = reinterpret_cast<SetCtx>(::GetProcAddress(user, "SetProcessDpiAwarenessContext"));
        if (pSet) {
        #ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        #elif defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)
            pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE); // PMv1 fallback
            return;
        #endif
        }
    }
    // Old OS fallback (Vista+). Benign if already DPI-aware via manifest.
    ::SetProcessDPIAware();
}
