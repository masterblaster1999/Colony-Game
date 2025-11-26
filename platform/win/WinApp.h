// platform/win/WinApp.h
#pragma once
#include <functional>
#include <utility>
#include <stdint.h>

struct WinCreateDesc {
    const wchar_t* title = L"Colony";
    struct { int w = 1280, h = 720; } clientSize;
    bool resizable     = true;
    bool debugConsole  = false;
    bool highDPIAware  = true;
};

class WinApp {
public:
    struct Callbacks {
        std::function<void(WinApp&, int width, int height, float dt)> onUpdate;
        std::function<void(WinApp&)>                                  onInit;
        std::function<void(WinApp&)>                                  onShutdown;
        std::function<void(WinApp&, int width, int height, float dt)> onRender;
        std::function<void(WinApp&, const wchar_t* path)>             onFileDrop;
    };
    // ...
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
