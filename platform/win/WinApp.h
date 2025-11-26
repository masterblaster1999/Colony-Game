#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ----------------------------------------------------------------------------
// Window creation descriptor — aligned with your launcher wiring.
// ----------------------------------------------------------------------------
struct WinCreateDesc {
    HINSTANCE      hInstance        = nullptr;
    const wchar_t* title            = L"Colony Game";

    // Preferred client size; if {0,0} we fall back to width/height.
    SIZE           clientSize       = { 0, 0 };
    int            width            = 1600;   // fallback client width
    int            height           = 900;    // fallback client height

    // Launcher flags you used earlier:
    bool           resizable        = true;   // toggles thickframe/maximize
    bool           debugConsole     = false;  // AllocConsole + stdio redirect
    bool           highDPIAware     = true;   // runtime PMv2 fallback (manifest preferred)

    // Style (resizable may adjust these).
    DWORD          style            = WS_OVERLAPPEDWINDOW;
    DWORD          exStyle          = 0;

    // Input/behavior:
    bool           rawInputNoLegacy = true;   // suppress legacy KB/mouse messages
};

// ----------------------------------------------------------------------------
// Win32 host with callback surface matching legacy call sites.
// ----------------------------------------------------------------------------
class WinApp {
public:
    struct Callbacks {
        // Lifecycle / frame
        std::function<void(WinApp&)>                   onInit;         // once before loop
        std::function<void(WinApp&, float /*dt*/)>     onUpdate;       // each frame (dt)
        std::function<void(WinApp&)>                   onRender;       // each frame
        std::function<void(WinApp&)>                   onShutdown;     // once after loop

        // File drop (WM_DROPFILES)
        std::function<void(WinApp&, const std::vector<std::wstring>&)> onFileDrop;

        // Input / windowing
        std::function<void(const RAWINPUT&)>           onRawInput;     // WM_INPUT
        std::function<void(WinApp&, int,int,float)>    onResize;       // WM_SIZE (w,h,dt=0)
        std::function<void(UINT,UINT)>                 onDpiChanged;   // WM_DPICHANGED (xDPI,yDPI)
    };

    // Legacy static API (kept for compatibility with main_win.cpp)
    static bool create(const WinCreateDesc& desc, const Callbacks& cbs);
    static int  run();
    static HWND hwnd();

    // Instance API
    bool Create(const WinCreateDesc& desc, const Callbacks& cbs);
    int  RunMessageLoop();
    HWND GetHwnd() const { return m_hwnd; }

private:
    static WinApp* s_self;
    HINSTANCE m_hInst = nullptr;
    HWND      m_hwnd  = nullptr;
    Callbacks m_cbs{};

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // Internals
    static void EnablePerMonitorV2DpiFallback(bool enable);  // API fallback; manifest preferred
    void RegisterRawInput(bool noLegacy);
    void EnableFileDrops(bool accept);
};
