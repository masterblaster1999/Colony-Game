#include "AppWindow.h"
#include <shellscalingapi.h> // AdjustWindowRectExForDpi
#pragma comment(lib, "Shcore.lib")

bool AppWindow::Create(HINSTANCE hInst, int nCmdShow, int width, int height)
{
    const wchar_t* kClass = L"ColonyWindowClass";
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &AppWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;

    RegisterClassExW(&wc);

    RECT r{0,0,(LONG)width,(LONG)height};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());

    m_hwnd = CreateWindowExW(
        0, kClass, L"Colony Game",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) return false;

    RegisterRawMouse(m_hwnd); // WM_INPUT

    RECT cr{};
    GetClientRect(m_hwnd, &cr);
    m_width = cr.right; m_height = cr.bottom;

    if (!m_gfx.Init(m_hwnd, m_width, m_height)) return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

void AppWindow::RegisterRawMouse(HWND hwnd)
{
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
    rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags     = RIDEV_INPUTSINK; // receive even if not focused
    rid.hwndTarget  = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        UINT w = LOWORD(lParam), h = HIWORD(lParam);
        m_width = w; m_height = h;
        if (w > 0 && h > 0) m_gfx.Resize(w, h);
        return 0;
    }
    case WM_DPICHANGED:
    {
        // Resize/move suggested rect for PMv2
        RECT* const prcNewWindow = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr,
            prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_INPUT:
    {
        // Get raw mouse delta
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        BYTE* lpb = new BYTE[dwSize];
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize)
        {
            RAWINPUT* raw = (RAWINPUT*)lpb;
            if (raw->header.dwType == RIM_TYPEMOUSE) {
                LONG dx = raw->data.mouse.lLastX;
                LONG dy = raw->data.mouse.lLastY;
                // TODO: feed into your camera/input system
            }
        }
        delete[] lpb;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int AppWindow::MessageLoop()
{
    MSG msg{};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        m_gfx.Render(m_vsync); // run your game loop / sim tick here
    }
}
