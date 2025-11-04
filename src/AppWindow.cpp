#include "AppWindow.h"
#include <shellscalingapi.h> // AdjustWindowRectExForDpi, GetDpiForSystem
#include <windowsx.h>        // GET_X_LPARAM, GET_Y_LPARAM, GET_WHEEL_DELTA_WPARAM
#include <vector>
#include <string>
#pragma comment(lib, "Shcore.lib")

// Local, file-private input/camera scratch so we don't change AppWindow.h yet.
namespace {
    struct MouseState {
        bool left   = false;
        bool right  = false;
        bool middle = false;
        int  x      = 0;
        int  y      = 0;
        bool hasPos = false;
    } g_mouse;

    // Simple placeholders for camera-ish controls.
    float g_yaw   = 0.f;
    float g_pitch = 0.f;
    float g_panX  = 0.f;
    float g_panY  = 0.f;
    float g_zoom  = 1.f;

    inline void ClampPitch() {
        if (g_pitch >  89.f) g_pitch =  89.f;
        if (g_pitch < -89.f) g_pitch = -89.f;
    }

    inline void BeginCapture(HWND hwnd) {
        SetCapture(hwnd); // capture mouse until a button goes up
    }

    inline void MaybeEndCapture() {
        if (!(g_mouse.left || g_mouse.right || g_mouse.middle)) {
            if (GetCapture()) ReleaseCapture();
        }
    }

    inline void UpdateDebugTitle(HWND hwnd) {
    #ifndef NDEBUG
        wchar_t title[256];
        swprintf_s(title, L"Colony Game  |  yaw %.1f  pitch %.1f  pan(%.1f, %.1f)  zoom %.2f",
                   g_yaw, g_pitch, g_panX, g_panY, g_zoom);
        SetWindowTextW(hwnd, title);
    #else
        (void)hwnd;
    #endif
    }

    // This is the "use dx/dy in the dispatch" piece: route raw/cursor deltas to actions.
    inline void ApplyDragFromDelta(HWND hwnd, LONG dx, LONG dy) {
        if (dx == 0 && dy == 0) return;

        if (g_mouse.left) {
            // LMB drag = orbit
            g_yaw   += static_cast<float>(dx) * 0.15f;
            g_pitch += static_cast<float>(dy) * 0.15f;
            ClampPitch();
        } else if (g_mouse.middle || g_mouse.right) {
            // MMB/RMB drag = pan
            g_panX += static_cast<float>(dx) * 0.02f;
            g_panY += static_cast<float>(dy) * 0.02f;
        }

        UpdateDebugTitle(hwnd);
    }
}

bool AppWindow::Create(HINSTANCE hInst, int nCmdShow, int width, int height)
{
    const wchar_t* kClass = L"ColonyWindowClass";
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &AppWindow::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    RECT r{0,0,(LONG)width,(LONG)height};
    // High-DPI aware rect sizing for the client area.
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem()); // MS docs. :contentReference[oaicite:4]{index=4}

    m_hwnd = CreateWindowExW(
        0, kClass, L"Colony Game",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) return false;

    RegisterRawMouse(m_hwnd); // enables WM_INPUT raw deltas (docs require registration). :contentReference[oaicite:5]{index=5}

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

    case WM_ERASEBKGND:
        // Avoid flicker; we redraw the entire client area.
        return 1;

    case WM_DPICHANGED:
    {
        // Resize/move suggested rect for PMv2
        // Per MS guidance, apply the suggested rectangle. :contentReference[oaicite:6]{index=6}
        RECT* const prcNewWindow = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr,
            prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT) {
            if (g_mouse.middle || g_mouse.right) { SetCursor(LoadCursor(nullptr, IDC_SIZEALL)); return TRUE; }
            if (g_mouse.left)                    { SetCursor(LoadCursor(nullptr, IDC_HAND));    return TRUE; }
        }
        break;
    }

    // --- Mouse buttons & move (cursor deltas) --------------------------------
    case WM_LBUTTONDOWN: {
        SetFocus(hWnd);
        g_mouse.left = true;
        BeginCapture(hWnd);
        g_mouse.x = GET_X_LPARAM(lParam);
        g_mouse.y = GET_Y_LPARAM(lParam);
        g_mouse.hasPos = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        g_mouse.left = false;
        MaybeEndCapture();
        return 0;
    }
    case WM_RBUTTONDOWN: {
        SetFocus(hWnd);
        g_mouse.right = true;
        BeginCapture(hWnd);
        g_mouse.x = GET_X_LPARAM(lParam);
        g_mouse.y = GET_Y_LPARAM(lParam);
        g_mouse.hasPos = true;
        return 0;
    }
    case WM_RBUTTONUP: {
        g_mouse.right = false;
        MaybeEndCapture();
        return 0;
    }
    case WM_MBUTTONDOWN: {
        SetFocus(hWnd);
        g_mouse.middle = true;
        BeginCapture(hWnd);
        g_mouse.x = GET_X_LPARAM(lParam);
        g_mouse.y = GET_Y_LPARAM(lParam);
        g_mouse.hasPos = true;
        return 0;
    }
    case WM_MBUTTONUP: {
        g_mouse.middle = false;
        MaybeEndCapture();
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (g_mouse.hasPos) {
            const int dx = x - g_mouse.x;
            const int dy = y - g_mouse.y;
            if (g_mouse.left || g_mouse.right || g_mouse.middle) {
                // Use cursor-space deltas when dragging (works w/o raw input too).
                ApplyDragFromDelta(hWnd, dx, dy);
            }
        }
        g_mouse.x = x; g_mouse.y = y; g_mouse.hasPos = true;
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        // Wheel detents are multiples of WHEEL_DELTA (120). :contentReference[oaicite:7]{index=7}
        const int detents = GET_WHEEL_DELTA_WPARAM(wParam) / 120;
        if (detents != 0) {
            g_zoom *= (1.0f + 0.10f * static_cast<float>(detents));
            if (g_zoom < 0.1f) g_zoom = 0.1f;
            if (g_zoom > 10.f) g_zoom = 10.f;
            UpdateDebugTitle(hWnd);
        }
        return 0;
    }

    // --- Raw input (high-resolution mouse deltas) -----------------------------
    case WM_INPUT:
    {
        // Per docs, use GetRawInputData to fetch RAWINPUT for this HRAWINPUT. :contentReference[oaicite:8]{index=8}
        UINT dwSize = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize) {
            std::vector<BYTE> buffer(dwSize);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const LONG dx = raw->data.mouse.lLastX;
                    const LONG dy = raw->data.mouse.lLastY;
                    // *** Use dx/dy in the dispatch (this fixes C4189 and wires drag/pan). ***
                    if (g_mouse.left || g_mouse.right || g_mouse.middle) {
                        ApplyDragFromDelta(hWnd, dx, dy);
                    }
                }
            }
        }
        return 0; // indicate we processed WM_INPUT
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
