#include "AppWindow_Impl.h"

// -------------------------------------------------------------------------------------------------
// AppWindow::WndProc
// -------------------------------------------------------------------------------------------------
// Keep the actual Win32 callback as small as possible; route everything through the AppWindow
// instance stored in GWLP_USERDATA.
//
// Message handling is split into two focused units implemented in:
//   - AppWindow_WndProc_Window.cpp  (focus, sizing, DPI, close/destroy)
//   - AppWindow_WndProc_Input.cpp   (keyboard, mouse, raw input)
//
// This keeps the WndProc readable and avoids a single “mega file” over time.

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMsg(hWnd, msg, wParam, lParam);

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool handled = false;

    // Window / sizing / focus / close handling first (keeps input code cleaner).
    {
        const LRESULT r = HandleMsg_Window(hWnd, msg, wParam, lParam, handled);
        if (handled)
            return r;
    }

#if defined(COLONY_WITH_IMGUI)
    // Let ImGui observe Win32 messages before we translate them into our own input events.
    // We intentionally do *not* early-return on ImGui handling so app-level hotkeys like
    // F11 (fullscreen) continue to work.
    if (m_impl && m_impl->imguiReady)
        (void)m_impl->imgui.handleWndProc(hWnd, msg, wParam, lParam);
#endif

    // Input handling (keyboard/mouse/rawinput).
    handled = false;
    {
        const LRESULT r = HandleMsg_Input(hWnd, msg, wParam, lParam, handled);
        if (handled)
            return r;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
