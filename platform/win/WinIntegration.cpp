// platform/win/WinIntegration.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "CrashHandler.h"   // lives in platform/win/

// Forward old free-function name to your class API.
void InstallCrashHandler(const wchar_t* dumpDir)
{
    CrashHandler::Install(dumpDir ? dumpDir : L".");
}

// Try to enable Per-Monitor (V2) DPI awareness as early as possible.
// Docs recommend manifest-based DPI selection; this API path is a supported fallback.
// Ref: SetProcessDpiAwarenessContext + PMv2 docs.
void TryEnablePerMonitorV2Dpi()
{
    // Prefer runtime lookup so older systems/toolchains still build and run.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetProcDpiCtxFn = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
        auto pSetCtx = reinterpret_cast<SetProcDpiCtxFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

        if (pSetCtx)
        {
        #ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        #elif defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)
            pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE); // PMv1 fallback if PMv2 not defined by SDK
            return;
        #else
            // No modern constants exposed by this SDK; fall through to system-aware.
        #endif
        }
    }

    // Final fallback for very old OS/SDK combos.
    // (Recommended approach is process manifest; calling after a window exists may have limited effect.)
    ::SetProcessDPIAware();
}
