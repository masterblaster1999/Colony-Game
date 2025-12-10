// Centralized Windows header policy: defines WIN32_LEAN_AND_MEAN, NOMINMAX, STRICT
// and includes <Windows.h> once for the entire project (prevents C4005).
#include "platform/win/WinCommon.h"

#include "AppWindow.h"
#include "CrashDump.h"

// --- Small Windows-only utilities (dynamic to keep compatibility) ----------------

namespace {

void HardenDllSearch()
{
    // Windows 8+ / KB2533623: restrict default DLL search path to safe locations.
    // If not present (older systems), this call will simply fail and we ignore it.
    using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
    if (k32)
    {
        auto pSetDefaultDllDirectories =
            reinterpret_cast<SetDefaultDllDirectories_t>(
                ::GetProcAddress(k32, "SetDefaultDllDirectories"));
        if (pSetDefaultDllDirectories)
        {
            // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS = safe search order
            pSetDefaultDllDirectories(0x00001000);
        }
    }
}

void ApplyDpiAwareness()
{
    // Microsoft recommends declaring PMv2 DPI in the app manifest.
    // This runtime path is a safe fallback as long as it runs BEFORE any HWND creation.
    // Try Per-Monitor V2 first (Win10+), then fall back to system DPI aware (Vista+).
    using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        auto pSetProcessDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContext_t>(
                ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (pSetProcessDpiAwarenessContext)
        {
            if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                return; // success
        }
    }

    // Fallback: system DPI aware (Vista+). Note: preferred solution is a manifest. 
    // This API is available on much older Windows and is better than being DPI-unaware.
    ::SetProcessDPIAware();
}

void NameMainThread()
{
    // Give the main thread a descriptive name for VS/WinDbg/WPA.
    using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);

    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
    if (k32)
    {
        auto pSetThreadDescription =
            reinterpret_cast<SetThreadDescription_t>(
                ::GetProcAddress(k32, "SetThreadDescription"));
        if (pSetThreadDescription)
        {
            pSetThreadDescription(::GetCurrentThread(), L"Main");
        }
    }
}

} // anonymous namespace
// -----------------------------------------------------------------------------

// Renamed from wWinMain to a normal function so that EntryWinMain.cpp
// can be the sole Windows entry point and delegate to this function.
int GameMain(HINSTANCE hInstance, PWSTR cmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(cmdLine);

    // Safer DLL search path (mitigates DLL preloading issues).
    HardenDllSearch(); // ok to call even if not present on older systems. 

    // DPI fallback (manifest is preferred). Must be before any HWND creation.
    ApplyDpiAwareness(); // PMv2 when available; falls back to system DPI aware.

    // Crash dumps for postmortem debugging (your existing facility).
    InstallCrashHandler(L"ColonyGame");

    // Name the main thread for friendlier diagnostics.
    NameMainThread(); // no-op if unsupported. 

    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, 1280, 720))
        return -1;

    return app.MessageLoop();
}
