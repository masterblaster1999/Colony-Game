#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct WinCreateDesc {
    HINSTANCE      hInstance       = nullptr;
    const wchar_t* title           = L"Colony Game";

    // Desired client-area size; used if both > 0.
    SIZE           clientSize      = { 0, 0 };

    // Fallback desired client size if clientSize is {0,0}.
    int            width           = 1600;
    int            height          = 900;

    // New flags expected by existing launcher code:
    bool           resizable       = true;   // if false: drop WS_THICKFRAME/WS_MAXIMIZEBOX
    bool           debugConsole    = false;  // if true: AllocConsole()
    bool           highDPIAware    = true;   // runtime fallback (manifest remains primary)

    DWORD          style           = WS_OVERLAPPEDWINDOW;
    DWORD          exStyle         = 0;

    // Input/boot strap options:
    bool           rawInputNoLegacy = true;
    bool           enableDpiFallback = true; // use runtime SetProcessDpiAwarenessContext
};

class WinApp {
public:
    struct Callbacks {
        // New lifecycle / frame callbacks expected by main_win.cpp:
        std::function<void(WinApp&)>                         onInit;      // after window creation
        std::function<void(WinApp&, int, int, float)>        onUpdate;    // per-frame
        std::function<void(WinApp&, int, int, float)>        onRender;    // per-frame
        std::function<void(WinApp&)>                         onShutdown;  // before destroy
        std::function<void(WinApp&, const std::vector<std::wstring>&)> onFileDrop; // WM_DROPFILES

        // Input / windowing:
        std::function<void(const RAWINPUT&)>                 onRawInput;      // WM_INPUT
        std::function<void(WinApp&, int, int, float)>        onResize;        // WM_SIZE (dt=0)
        std::function<void(UINT,UINT)>                       onDpiChanged;    // WM_DPICHANGED
        std::function<void()>                                onClose;         // WM_CLOSE
    };

    // Legacy static API (kept for existing code)
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
    static void EnableDebugConsoleIfRequested(bool enable);
};
