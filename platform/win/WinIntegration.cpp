// platform/win/WinIntegration.cpp
// Windows-only shims to preserve legacy call sites in WinLauncher.cpp

#include "WinIntegration.h"
#include "CrashHandler.h"      // your crash handler (uses SetUnhandledExceptionFilter + MiniDumpWriteDump)
#include <windows.h>           // don't define WIN32_LEAN_AND_MEAN here; let build do it

// NEW: zero-arg overload to satisfy WinLauncher.cpp()
void InstallCrashHandler()
{
    // Use current directory by default (or change to a better default if you prefer).
    CrashHandler::Install(L".");
}

// Existing 1-arg overload
void InstallCrashHandler(const wchar_t* dumpDir)
{
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
