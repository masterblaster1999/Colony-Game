// Platform/Win/WinMain.cpp 

// Make sure we don't trigger C4005 if WIN32_LEAN_AND_MEAN is also
// defined on the compiler command line.
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellscalingapi.h>   // manifest is preferred; API as fallback
#pragma comment(lib, "Shcore.lib")

// Optional helper to initialize DPI awareness in a safer way.
// Tries the modern per-monitor-v2 mode first, then falls back
// to older APIs if needed.
static bool InitDpiAwareness()
{
    // Windows 10+ (per-monitor V2). If this succeeds we’re done.
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return true;

    // Windows 8.1+ Shcore API (PROCESS_PER_MONITOR_DPI_AWARE).
    // E_ACCESSDENIED just means DPI awareness was already set, which is fine.
    HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    if (SUCCEEDED(hr) || hr == E_ACCESSDENIED)
        return true;

    // Vista+ legacy API. If this fails too, we just stay not DPI-aware.
    if (SetProcessDPIAware())
        return true;

    return false;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // Avoid unused-parameter warnings when /WX is enabled.
    (void)hInst;

    // Prefer manifest-based DPI awareness; this runtime call is a
    // safety net during dev builds or if the manifest is missing.
    InitDpiAwareness();

    // TODO: create window, show it, and run your main game loop here,
    // or delegate to your engine's platform-agnostic entry point.
    // e.g.:
    // return RunColonyGame(hInst);

    return 0; // explicit exit code keeps MSVC happy
}
