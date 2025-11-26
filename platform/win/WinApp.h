#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// Description of the window to create (Windows-only).
struct WinCreateDesc {
    HINSTANCE      hInstance        = nullptr;
    const wchar_t* title            = L"Colony Game";

    // Preferred client-area size. If {0,0}, fall back to width/height.
    SIZE           clientSize       = { 0, 0 };
    int            width            = 1600;   // legacy fallback (client width)
    int            height           = 900;    // legacy fallback (client height)

    DWORD          style            = WS_OVERLAPPEDWINDOW;
    DWORD          exStyle          = 0;

    bool           resizable        = true;   // drop WS_THICKFRAME/WS_MAXIMIZEBOX if false
    bool           debugConsole     = false;  // Alloc/attach console for stdout/stderr
    bool           highDPIAware     = true;   // prefer Per-Monitor V2; runtime fallback ok
    bool           rawInputNoLegacy = true;   // suppress legacy KB/mouse messages
    bool           enableDpiFallback= true;   // call SetProcessDpiAwarenessContext at boot
};

class WinApp {
public:
    struct Callbacks {
        // Game lifecycle
        std::function<void(WinApp&)>    onInit;        // once before loop
        std::function<void(WinApp&, float /*dt*/)> onUpdate;  // each frame (dt only)
        std::function<void(WinApp&)>    onRender;      // each frame
        std::function<void()>           onShutdown;    // once after loop
        std::function<void(WinApp&, const std::vector<std::wstring>&)> onFileDrop; // WM_DROPFILES

        // Platform/events
        std::function<void(const RAWINPUT&)> onRawInput;    // WM_INPUT
        std::function<void(UINT,UINT)>       onResize;      // WM_SIZE (client W,H)
        std::function<void(UINT,UINT)>       onDpiChanged;  // WM_DPICHANGED (xDPI,yDPI)
        std::function<void()>                onClose;       // WM_CLOSE
    };

    // Legacy static API (kept for main_win.cpp compatibility)
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
    void RegisterRawInput(bool noLegacy);
    static void EnablePerMonitorV2DpiFallback();
};
