#include "AppWindow_Impl.h"

// -------------------------------------------------------------------------------------------------
// AppWindow message handling: Window / focus / sizing / DPI / lifetime
// -------------------------------------------------------------------------------------------------

LRESULT AppWindow::HandleMsg_Window(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    switch (msg)
    {
    case WM_SETFOCUS:
        if (m_impl) m_impl->mouse.OnSetFocus();
        handled = true;
        return 0;

    case WM_KILLFOCUS:
        if (m_impl) {
            // Drop any buffered mouse deltas on focus loss to avoid applying
            // stale movement when focus returns.
            m_impl->pendingMouseDx = 0;
            m_impl->pendingMouseDy = 0;

            m_impl->mouse.OnKillFocus(hWnd);

            // Flush keyboard state on focus loss to avoid "stuck key" behavior
            // (KeyUp may not be delivered once focus is gone).
            colony::input::InputEvent ev{};
            ev.type = colony::input::InputEventType::FocusLost;
            m_impl->input.Push(ev);
        }
        handled = true;
        return 0;

    case WM_ACTIVATEAPP:
        if (m_impl) {
            const bool active = wParam != 0;
            m_impl->active = active;
            m_impl->mouse.OnActivateApp(hWnd, active);
            if (!active) {
                // Drop any buffered mouse deltas when we go inactive.
                m_impl->pendingMouseDx = 0;
                m_impl->pendingMouseDy = 0;

                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::FocusLost;
                m_impl->input.Push(ev);
            }

            // Reflect active/background state in the debug title.
            UpdateTitle();
        }
        handled = true;
        return 0;

    case WM_ENTERSIZEMOVE:
        if (m_impl) {
            m_impl->inSizeMove = true;
            m_impl->pendingResizeW = 0;
            m_impl->pendingResizeH = 0;
        }
        handled = true;
        return 0;

    case WM_EXITSIZEMOVE:
        if (m_impl) {
            m_impl->inSizeMove = false;

            // If we deferred swapchain resizing during the sizing drag, apply the
            // final size once.
            if (m_impl->pendingResizeW > 0 && m_impl->pendingResizeH > 0)
            {
                const UINT finalW = m_impl->pendingResizeW;
                const UINT finalH = m_impl->pendingResizeH;

                m_gfx.Resize(finalW, finalH);
                m_impl->pendingResizeW = 0;
                m_impl->pendingResizeH = 0;

                // Notify the game layer once (final size). This is useful for
                // future UI/layout code without spamming events during the drag.
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::WindowResize;
                ev.width = finalW;
                ev.height = finalH;
                m_impl->input.Push(ev);
            }
        }
        handled = true;
        return 0;

    case WM_CAPTURECHANGED:
        if (m_impl) m_impl->mouse.OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
        handled = true;
        return 0;

    case WM_CANCELMODE:
        if (m_impl) m_impl->mouse.OnCancelMode(hWnd);
        handled = true;
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        handled = true;
        return 0;

    case WM_DESTROY:
        if (m_impl)
        {
            // Capture the latest known state before saving.
            m_impl->settings.vsync = m_vsync;
            m_impl->settings.fullscreen = m_impl->fullscreen.IsFullscreen();
            if (!m_impl->fullscreen.IsFullscreen() && m_width > 0 && m_height > 0)
            {
                m_impl->settings.windowWidth = m_width;
                m_impl->settings.windowHeight = m_height;
            }

            // Save even if unchanged; the file is tiny and this makes first-run deterministic.
            (void)colony::appwin::SaveUserSettings(m_impl->settings);
        }

        PostQuitMessage(0);
        handled = true;
        return 0;

    case WM_GETMINMAXINFO:
        // Enforce a minimum *client* size so the renderer and UI don't end up
        // in pathological states (tiny swapchains, unreadable HUD, etc.).
        //
        // We translate the desired client minimum into a window minimum using
        // AdjustWindowRectExForDpi so it remains correct under per-monitor DPI.
        if (lParam)
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

            const DWORD style   = static_cast<DWORD>(GetWindowLongW(hWnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hWnd, GWL_EXSTYLE));

            RECT rc{ 0, 0,
                     static_cast<LONG>(colony::appwin::kMinWindowClientWidth),
                     static_cast<LONG>(colony::appwin::kMinWindowClientHeight) };

            const UINT dpi = GetDpiForWindow(hWnd);
            AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);

            mmi->ptMinTrackSize.x = rc.right - rc.left;
            mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        }
        handled = true;
        return 0;

    case WM_SIZE:
    {
        const UINT w = LOWORD(lParam);
        const UINT h = HIWORD(lParam);
        m_width = w;
        m_height = h;

        // During interactive sizing drags, resizing the swapchain on every WM_SIZE can
        // cause stutter and (with debug layers) spew DXGI warnings. Defer until
        // WM_EXITSIZEMOVE so we only resize once at the final dimensions.
        if (m_impl && m_impl->inSizeMove)
        {
            m_impl->pendingResizeW = w;
            m_impl->pendingResizeH = h;
        }
        else
        {
            if (w > 0 && h > 0)
                m_gfx.Resize(w, h);
        }

        // Notify the game layer immediately when the resize isn't part of an
        // interactive sizing drag (those are emitted once from WM_EXITSIZEMOVE).
        if (m_impl && w > 0 && h > 0 && !m_impl->inSizeMove)
        {
            colony::input::InputEvent ev{};
            ev.type = colony::input::InputEventType::WindowResize;
            ev.width = w;
            ev.height = h;
            m_impl->input.Push(ev);
        }

        // Persist windowed dimensions only (fullscreen sizes are monitor-dependent).
        if (m_impl && w > 0 && h > 0 && !m_impl->fullscreen.IsFullscreen())
        {
            m_impl->settings.windowWidth = w;
            m_impl->settings.windowHeight = h;
            m_impl->ScheduleSettingsAutosave();
        }

        handled = true;
        return 0;
    }

    case WM_ERASEBKGND:
        // Avoid flicker; we redraw the entire client area.
        handled = true;
        return 1;

    case WM_DPICHANGED:
        colony::appwin::win32::ApplyDpiSuggestedRect(hWnd, reinterpret_cast<const RECT*>(lParam));
        handled = true;
        return 0;

    default:
        break;
    }

    handled = false;
    return 0;
}
