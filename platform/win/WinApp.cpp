// platform/win/WinApp.cpp
#include "WinApp.h"
#include <shellscalingapi.h>   // optional fallback API
#include <pathcch.h>

#pragma comment(lib, "Pathcch.lib")
#pragma comment(lib, "Shcore.lib")   // if you ever call SetProcessDpiAwareness

WinApp* WinApp::s_self = nullptr;

void WinApp::EnablePerMonitorV2DpiFallback() {
    // Microsoft: prefer manifest for default DPI awareness. API fallback is allowed, but not recommended. :contentReference[oaicite:3]{index=3}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void WinApp::RegisterRawInput(bool noLegacy) {
    RAWINPUTDEVICE rids[2]{};

    // Keyboard
    rids[0].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rids[0].usUsage     = 0x06;  // KEYBOARD
    rids[0].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0);
    rids[0].hwndTarget  = m_hwnd;

    // Mouse
    rids[1].usUsagePage = 0x01;  // GENERIC
    rids[1].usUsage     = 0x02;  // MOUSE
    rids[1].dwFlags     = 0;
    rids[1].hwndTarget  = m_hwnd;

    RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE)); // Required to receive WM_INPUT. :contentReference[oaicite:4]{index=4}
}

bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs) {
    m_hInst = desc.hInstance ? desc.hInstance : GetModuleHandleW(nullptr);
    m_cbs = cbs;

    if (desc.enableDpiFallback) EnablePerMonitorV2DpiFallback();  // fallback; manifest preferred. :contentReference[oaicite:5]{index=5}

    WNDCLASSW wc{};
    wc.hInstance     = m_hInst;
    wc.lpszClassName = L"ColonyGameWindowClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return false;

    RECT r{0,0,desc.width,desc.height};
    AdjustWindowRectEx(&r, desc.style, FALSE, desc.exStyle);

    m_hwnd = CreateWindowExW(
        desc.exStyle, wc.lpszClassName, desc.title, desc.style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    RegisterRawInput(desc.rawInputNoLegacy);
    return true;
}

int WinApp::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

// ---- Legacy static API wrappers ----
bool WinApp::create(const WinCreateDesc& d, const Callbacks& c) {
    if (!s_self) s_self = new WinApp();
    return s_self->Create(d, c);
}
int WinApp::run() {
    return s_self ? s_self->RunMessageLoop() : -1;
}
HWND WinApp::hwnd() {
    return s_self ? s_self->GetHwnd() : nullptr;
}

// ---- WndProc ----
LRESULT CALLBACK WinApp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinApp* self = s_self;
    switch (msg) {
    case WM_INPUT: {
        if (self && self->m_cbs.onRawInput) {
            UINT size = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            std::unique_ptr<BYTE[]> buf(new BYTE[size]);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.get(), &size, sizeof(RAWINPUTHEADER)) == size) {
                self->m_cbs.onRawInput(*reinterpret_cast<RAWINPUT*>(buf.get()));
            }
        }
        return 0;
    } // Raw input prerequisites and WM_INPUT behavior. :contentReference[oaicite:6]{index=6}

    case WM_SIZE:
        if (self && self->m_cbs.onResize) {
            self->m_cbs.onResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (self && self->m_cbs.onDpiChanged) {
            const UINT dpi = HIWORD(wParam); // x and y are equal in most cases
            self->m_cbs.onDpiChanged(dpi, dpi);
        }
        return 0;
    } // Use suggested rect per Microsoft DPI doc. :contentReference[oaicite:7]{index=7}

    case WM_CLOSE:
        if (self && self->m_cbs.onClose) self->m_cbs.onClose();
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
