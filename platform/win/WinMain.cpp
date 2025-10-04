// Platform/Win/WinMain.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h> // manifest is preferred; API as fallback
#pragma comment(lib, "Shcore.lib")

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // Prefer manifest-based DPI awareness; API fallback as last resort
    // (MS docs recommend manifest, but this helps during dev builds)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ... create window, show, run loop ...
}
