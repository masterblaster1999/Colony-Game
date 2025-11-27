// platform/win/WinApp.h
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>   // HWND, RAWINPUT, etc.
#include <functional>
#include <utility>
#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Window creation description
// ----------------------------------------------------------------------------
struct WinCreateDesc {
    const wchar_t* title = L"Colony";
    struct { int w = 1280, h = 720; } clientSize;
    bool resizable     = true;
    bool debugConsole  = false;
    bool highDPIAware  = true;
};

// ----------------------------------------------------------------------------
// Win32 host with callback surface matching legacy call sites.
// ----------------------------------------------------------------------------
class WinApp {
public:
    struct Callbacks {
        // Lifecycle / frame
        std::function<void(WinApp&)>               onInit;         // once before loop
        std::function<void(WinApp&, float /*dt*/)> onUpdate;       // each frame (dt)
        std::function<void(WinApp&)>               onRender;       // each frame
        std::function<void(WinApp&)>               onShutdown;     // once after loop

        // File drop (WM_DROPFILES)
        std::function<void(WinApp&, const std::vector<std::wstring>&)> onFileDrop;

        // Input / windowing
        std::function<void(const RAWINPUT&)>       onRawInput;     // WM_INPUT (raw struct, if needed)
        std::function<void(WinApp&, int,int,float)> onResize;      // WM_SIZE (w,h,dpiScale)
        std::function<void(UINT,UINT)>             onDpiChanged;   // WM_DPICHANGED (xDPI,yDPI)

        // --- New optional raw-input convenience slots ---
        // Mouse raw delta: dx, dy (relative unless isAbsolute==true)
        std::function<void(WinApp&, LONG, LONG, bool)> onMouseRawDelta;
        // Mouse wheel: delta (WHEEL_DELTA multiples). horizontal==true for tilt wheel.
        std::function<void(WinApp&, short, bool)>      onMouseWheel;
        // Keyboard: Win32 virtual key (L/R variants when distinguishable), down?
        std::function<void(WinApp&, unsigned short, bool)> onKeyRaw;
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
