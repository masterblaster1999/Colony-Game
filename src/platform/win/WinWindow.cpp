#include "WinWindow.h"
#include <windows.h>

namespace platform::win {

static const wchar_t* kWindowClass = L"ColonyGameWindowClass";

WinWindow::WinWindow() = default;
WinWindow::~WinWindow() = default;

bool WinWindow::Create(const wchar_t* title, int width, int height) {
    m_hinst = GetModuleHandleW(nullptr);

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &WinWindow::WndProc;
    wc.hInstance     = m_hinst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    // Calculate size incl. non-client area
    RECT rc{0,0,width,height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    // Create window (hidden initially)
    m_hwnd = CreateWindowExW(
        0, kWindowClass, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, m_hinst, this);

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

bool WinWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void WinWindow::GetClientSize(unsigned& w, unsigned& h) const {
    RECT r{};
    GetClientRect(m_hwnd, &r);
    w = static_cast<unsigned>(r.right - r.left);
    h = static_cast<unsigned>(r.bottom - r.top);
}

LRESULT CALLBACK WinWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinWindow* self;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<WinWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<WinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WinWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace platform::win
