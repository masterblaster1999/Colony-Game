#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <functional>

struct WinCreateDesc {
    HINSTANCE      hInstance       = nullptr;
    const wchar_t* title           = L"Colony Game";

    // NEW: desired client-area size. If both > 0, this is used.
    // Otherwise, we fall back to (width/height) below as desired client size.
    SIZE           clientSize      = { 0, 0 };   // {cx, cy}

    // Kept for backward-compat: treated as desired client size if clientSize={0,0}.
    int            width           = 1600;
    int            height          = 900;

    DWORD          style           = WS_OVERLAPPEDWINDOW;
    DWORD          exStyle         = 0;
    bool           rawInputNoLegacy= true;
    bool           enableDpiFallback = true;     // manifest is primary; this is a safety net
};

class WinApp {
public:
    struct Callbacks {
        std::function<void(const RAWINPUT&)> onRawInput;     // WM_INPUT
        std::function<void(UINT,UINT)>       onResize;       // WM_SIZE
        std::function<void(UINT,UINT)>       onDpiChanged;   // WM_DPICHANGED
        std::function<void()>                onClose;        // WM_CLOSE
    };

    // Legacy static API (expected by main_win.cpp)
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
