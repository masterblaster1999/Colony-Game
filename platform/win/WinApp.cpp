#include "WinApp.h"
#include <hidusage.h>    // HID usages for Raw Input
#include <shellscalingapi.h> // SetProcessDpiAwareness (SHCore)
#pragma comment(lib, "Shcore.lib")

WinApp* WinApp::self_ = nullptr;

void WinApp::EnablePerMonitorV2DpiAwareness() {
    // Microsoft: prefer manifest; this is only a fallback.
    // Try user32!SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2), else fall back to SHCore. :contentReference[oaicite:1]{index=1}
    HMODULE user32 = ::LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto fn = reinterpret_cast<SetCtx>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                ::FreeLibrary(user32);
                return;
            }
        }
        ::FreeLibrary(user32);
    }
    // Downlevel fallback (Windows 8.1 API) if needed: System or PerMonitor. :contentReference[oaicite:2]{index=2}
    ::SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
}

bool WinApp::CreateWindowClassAndWindow(const std::wstring& title, int w, int h, bool borderless) {
    self_ = this;

    WNDCLASSW wc{};
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ColonyGameWndClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return false;

    DWORD style   = borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    DWORD exstyle = borderless ? WS_EX_APPWINDOW : 0;

    RECT r{0,0,w,h};
    ::AdjustWindowRectEx(&r, style, FALSE, exstyle);

    hwnd_ = ::CreateWindowExW(
        exstyle, wc.lpszClassName, title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, wc.hInstance, nullptr);

    return hwnd_ != nullptr;
}

void WinApp::RegisterRawInput(bool noLegacy) {
    // Register for keyboard + mouse raw input. WM_INPUT will be delivered to our WndProc. :contentReference[oaicite:3]{index=3}
    RAWINPUTDEVICE rids[2]{};

    // Keyboard
    rids[0].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rids[0].usUsage     = 0x06;  // HID_USAGE_GENERIC_KEYBOARD
    rids[0].dwFlags     = RIDEV_INPUTSINK | (noLegacy ? RIDEV_NOLEGACY : 0);
    rids[0].hwndTarget  = hwnd_;

    // Mouse
    rids[1].usUsagePage = 0x01;
    rids[1].usUsage     = 0x02;  // HID_USAGE_GENERIC_MOUSE
    rids[1].dwFlags     = RIDEV_INPUTSINK | (noLegacy ? RIDEV_NOLEGACY : 0);
    rids[1].hwndTarget  = hwnd_;

    ::RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE)); // enables WM_INPUT. :contentReference[oaicite:4]{index=4}
}

int WinApp::RunMessageLoop() {
    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);

    MSG msg{};
    while (true) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return int(msg.wParam);
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        // TODO: call your game tick/render here
    }
}

LRESULT CALLBACK WinApp::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INPUT: {
        // Read per-message RAWINPUT when needed. :contentReference[oaicite:5]{index=5}
        UINT size = 0;
        ::GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        std::unique_ptr<BYTE[]> buf(new BYTE[size]);
        if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, buf.get(), &size, sizeof(RAWINPUTHEADER)) == size) {
            // const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(buf.get());
            // TODO: forward ri->data.keyboard / ri->data.mouse to your input system
        }
        return 0;
    }
    case WM_DPICHANGED: {
        // Resize to suggested rect per Microsoft guidance for PMv2. :contentReference[oaicite:6]{index=6}
        const RECT* suggested = reinterpret_cast<RECT*>(l);
        ::SetWindowPos(h, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(h, m, w, l);
}

