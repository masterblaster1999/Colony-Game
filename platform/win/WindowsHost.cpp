#include "WindowsHost.h"
#include <hidusage.h> // for HID usages
WindowsHost* WindowsHost::s_self = nullptr;

static const wchar_t* kWndClass = L"ColonyGameWindow";

bool WindowsHost::Create(HINSTANCE hInst, int nCmdShow) {
    s_self = this;

    // Register class (CS_OWNDC helpful for GL/DX interop)
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &WindowsHost::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    // Create top-level window
    m_hwnd = CreateWindowExW(0, kWndClass, L"Colony Game",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             1600, 900, nullptr, nullptr, hInst, nullptr);
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    RegisterRawInput(); // keyboard + mouse via Raw Input
    return true;
}

void WindowsHost::RegisterRawInput() {
    RAWINPUTDEVICE rid[2]{};

    // Keyboard
    rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[0].usUsage     = HID_USAGE_GENERIC_KEYBOARD;
    rid[0].dwFlags     = RIDEV_NOLEGACY;     // no legacy WM_KEY* synthesis
    rid[0].hwndTarget  = m_hwnd;

    // Mouse
    rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[1].usUsage     = HID_USAGE_GENERIC_MOUSE;
    rid[1].dwFlags     = RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE; // requires NOLEGACY
    rid[1].hwndTarget  = m_hwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    // RIDEV_CAPTUREMOUSE can be used only with NOLEGACY; otherwise registration fails. :contentReference[oaicite:11]{index=11}
}

int WindowsHost::MessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK WindowsHost::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT: {
        // Raw input is delivered here; use GetRawInputData() to consume. :contentReference[oaicite:12]{index=12}
        break;
    }
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
