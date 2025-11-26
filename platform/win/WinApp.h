// platform/win/WinApp.h
#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <functional>

struct WinCreateDesc {
    HINSTANCE hInstance = nullptr;
    const wchar_t* title = L"Colony Game";
    int width  = 1600;
    int height = 900;
    DWORD style  = WS_OVERLAPPEDWINDOW;
    DWORD exStyle= 0;
    bool  rawInputNoLegacy = true;     // if true, suppress legacy WM_* key msgs
    bool  enableDpiFallback = true;    // manifest is preferred; API is fallback
};

class WinApp {
public:
    struct Callbacks {
        // Install any you need; leave null if unused
        std::function<void(const RAWINPUT&)> onRawInput;     // WM_INPUT
        std::function<void(UINT,UINT)>       onResize;       // WM_SIZE
        std::function<void(UINT,UINT)>       onDpiChanged;   // WM_DPICHANGED
        std::function<void()>                onClose;        // WM_CLOSE
    };

    // --- Legacy static API expected by your main_win.cpp ---
    static bool create(const WinCreateDesc& desc, const Callbacks& cbs);
    static int  run();
    static HWND hwnd();

    // --- Thin instance API (you can use directly elsewhere) ---
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

    // DPI fallback (manifest is recommended way; this is just a safety net)
    static void EnablePerMonitorV2DpiFallback();
};
