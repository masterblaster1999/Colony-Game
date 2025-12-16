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

    // Track whether the window/app is active/focused. Used to avoid stuck buttons
    // and to ignore raw input when we're not the active window.
    bool g_hasFocus = true;

    // Track whether raw mouse input was successfully registered. If true, prefer WM_INPUT
    // deltas and avoid double-applying deltas from WM_MOUSEMOVE while dragging.
    bool g_rawMouseRegistered = false;

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

    inline void ClearMouseStateAndCapture(HWND hwnd) {
        g_mouse.left = false;
        g_mouse.right = false;
        g_mouse.middle = false;
        g_mouse.hasPos = false;

        // Only release if we are the capture owner for this thread.
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
    }

    inline void MaybeEndCapture(HWND hwnd) {
        if (!(g_mouse.left || g_mouse.right || g_mouse.middle)) {
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
        }
    }

    inline bool InputActive(HWND hwnd) {
        // Either we have focus, or we still own capture (dragging outside client).
        return g_hasFocus || (GetCapture() == hwnd);
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
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());

    m_hwnd = CreateWindowExW(
        0, kClass, L"Colony Game",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) return false;

    RegisterRawMouse(m_hwnd); // enables WM_INPUT raw deltas (docs require registration).

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

    // Keep INPUTSINK so we continue receiving WM_INPUT even while captured; we still
    // gate processing by focus/capture in WM_INPUT to avoid background movement.
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;

    g_rawMouseRegistered = (RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE);
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
    case WM_SETFOCUS:
    {
        g_hasFocus = true;
        // Reset cursor delta history to avoid a huge "first move" delta after refocus.
        g_mouse.hasPos = false;
        return 0;
    }

    case WM_KILLFOCUS:
    {
        g_hasFocus = false;
        // Focus lost while dragging can prevent mouse-up from arriving -> clear state.
        ClearMouseStateAndCapture(hWnd);
        return 0;
    }

    case WM_ACTIVATEAPP:
    {
        // App-level activation toggles (alt-tab etc).
        if (wParam) {
            g_hasFocus = true;
            g_mouse.hasPos = false;
        } else {
            g_hasFocus = false;
            ClearMouseStateAndCapture(hWnd);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
    {
        // lParam is the window gaining capture (can be nullptr). If it isn't us,
        // we should clear our internal button state to avoid "stuck dragging".
        const HWND newCapture = reinterpret_cast<HWND>(lParam);
        if (newCapture != hWnd) {
            ClearMouseStateAndCapture(hWnd);
        }
        return 0;
    }

    case WM_CANCELMODE:
    {
        // Cancels modes such as mouse capture (e.g., modal loops).
        ClearMouseStateAndCapture(hWnd);
        return 0;
    }

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
        // Per MS guidance, apply the suggested rectangle.
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
        MaybeEndCapture(hWnd);
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
        MaybeEndCapture(hWnd);
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
        MaybeEndCapture(hWnd);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        if (g_mouse.hasPos) {
            const int dx = x - g_mouse.x;
            const int dy = y - g_mouse.y;

            if ((g_mouse.left || g_mouse.right || g_mouse.middle) && InputActive(hWnd)) {
                // If raw input is registered, prefer WM_INPUT deltas and avoid double-applying.
                if (!g_rawMouseRegistered) {
                    // Use cursor-space deltas when dragging (works w/o raw input too).
                    ApplyDragFromDelta(hWnd, dx, dy);
                }
            }
        }

        g_mouse.x = x; g_mouse.y = y; g_mouse.hasPos = true;
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        // Wheel detents are multiples of WHEEL_DELTA (120).
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
        // Only process raw input when the window is active or owns capture,
        // and only while dragging (buttons down). This avoids background movement
        // and reduces per-message overhead.
        if (!InputActive(hWnd) || !(g_mouse.left || g_mouse.right || g_mouse.middle)) {
            return 0;
        }

        // Per docs, use GetRawInputData to fetch RAWINPUT for this HRAWINPUT.
        UINT dwSize = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize) {
            // Reuse a static buffer to avoid heap churn at high WM_INPUT rates.
            static std::vector<BYTE> s_buffer;
            if (s_buffer.size() < dwSize) {
                s_buffer.resize(dwSize);
            }

            const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                                                RID_INPUT,
                                                s_buffer.data(),
                                                &dwSize,
                                                sizeof(RAWINPUTHEADER));
            if (copied == dwSize) {
                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(s_buffer.data());
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const LONG dx = raw->data.mouse.lLastX;
                    const LONG dy = raw->data.mouse.lLastY;

                    // Use dx/dy in the dispatch.
                    ApplyDragFromDelta(hWnd, dx, dy);
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

    // High-resolution timing for a lightweight frame limiter (vsync OFF).
    LARGE_INTEGER qpcFreq{};
    QueryPerformanceFrequency(&qpcFreq);

    // When vsync is OFF, cap to avoid pegging a CPU core at 100%.
    // (Keep this conservative-high; you can wire it to a setting later.)
    constexpr int kMaxFpsWhenVsyncOff = 240;
    const LONGLONG ticksPerFrame =
        (kMaxFpsWhenVsyncOff > 0) ? (qpcFreq.QuadPart / kMaxFpsWhenVsyncOff) : 0;

    LONGLONG nextFrameQpc = 0;
    bool lastVsync = m_vsync;

    while (true)
    {
        // Reset pacing if vsync changes at runtime.
        if (lastVsync != m_vsync) {
            nextFrameQpc = 0;
            lastVsync = m_vsync;
        }

        // If minimized, don't render; just block until something happens.
        if (m_width == 0 || m_height == 0)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            WaitMessage();
            continue;
        }

        // Frame pacing (vsync OFF):
        // Wait until either the next frame time arrives or we receive input/messages.
        if (!m_vsync && ticksPerFrame > 0)
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);

            if (nextFrameQpc == 0) {
                // First frame: render immediately.
                nextFrameQpc = now.QuadPart;
            }

            const LONGLONG remaining = nextFrameQpc - now.QuadPart;
            if (remaining > 0)
            {
                const DWORD waitMs = static_cast<DWORD>((remaining * 1000) / qpcFreq.QuadPart);

                if (waitMs > 0) {
                    // Wait for either messages or timeout.
                    MsgWaitForMultipleObjectsEx(
                        0, nullptr,
                        waitMs,
                        QS_ALLINPUT,
                        MWMO_INPUTAVAILABLE
                    );
                } else {
                    // Very small remainder; yield to avoid hot spinning.
                    Sleep(0);
                }
            }
        }

        // Pump all queued messages.
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Re-check minimized state after message pump.
        if (m_width == 0 || m_height == 0) {
            continue;
        }

        // If vsync is OFF and we woke due to messages, don't render early; wait until scheduled time.
        if (!m_vsync && ticksPerFrame > 0)
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            if (nextFrameQpc != 0 && now.QuadPart < nextFrameQpc) {
                continue;
            }
        }

        // Render one frame (your sim tick could go here too).
        m_gfx.Render(m_vsync);

        // Advance pacing schedule (vsync OFF).
        if (!m_vsync && ticksPerFrame > 0)
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);

            if (nextFrameQpc == 0) {
                nextFrameQpc = now.QuadPart;
            }

            nextFrameQpc += ticksPerFrame;

            // If we're far behind (breakpoints / long hitch), resync to avoid a spiral.
            if (now.QuadPart > nextFrameQpc + (ticksPerFrame * 8)) {
                nextFrameQpc = now.QuadPart + ticksPerFrame;
            }
        }
    }
}
