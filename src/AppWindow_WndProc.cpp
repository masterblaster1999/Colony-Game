#include "AppWindow_Impl.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM

#include <cstdint>

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
    // Note: we keep the OS-facing switch here, but route the "heavy" logic
    // into small modules to keep AppWindow.cpp readable.
    switch (msg)
    {
    case WM_SETFOCUS:
        if (m_impl) m_impl->mouse.OnSetFocus();
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
        return 0;

    case WM_ENTERSIZEMOVE:
        if (m_impl) {
            m_impl->inSizeMove = true;
            m_impl->pendingResizeW = 0;
            m_impl->pendingResizeH = 0;
        }
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
        return 0;

    case WM_CAPTURECHANGED:
        if (m_impl) m_impl->mouse.OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_CANCELMODE:
        if (m_impl) m_impl->mouse.OnCancelMode(hWnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
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

        return 0;
    }

    case WM_ERASEBKGND:
        // Avoid flicker; we redraw the entire client area.
        return 1;

    case WM_DPICHANGED:
        colony::appwin::win32::ApplyDpiSuggestedRect(hWnd, reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_impl) {
            const auto b = m_impl->mouse.Buttons();
            if (b.middle || b.right || b.x1 || b.x2) { SetCursor(LoadCursor(nullptr, IDC_SIZEALL)); return TRUE; }
            if (b.left)              { SetCursor(LoadCursor(nullptr, IDC_HAND));    return TRUE; }
        }
        break;

    // ---------------------------------------------------------------------
    // Keyboard
    // ---------------------------------------------------------------------
    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_ESCAPE:
            PostQuitMessage(0);
            return 0;

        case VK_F11:
            // Ignore auto-repeat so holding F11 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0)
                ToggleFullscreen();
            return 0;

        case VK_F10:
            // Toggle title-bar frame pacing stats (PresentMon-style summary)
            // Ignore auto-repeat so holding F10 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                m_impl->settings.showFrameStats = !m_impl->settings.showFrameStats;
                m_impl->frameStats.Reset();
                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            return 0;

        case VK_F9:
            // Toggle raw mouse input at runtime (best-effort).
            // Ignore auto-repeat so holding F9 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                m_impl->settings.rawMouse = !m_impl->settings.rawMouse;
                m_impl->mouse.SetEnabled(hWnd, m_impl->settings.rawMouse);

                // Drop any pending deltas to avoid a jump across the mode switch.
                m_impl->pendingMouseDx = 0;
                m_impl->pendingMouseDy = 0;

                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            return 0;

        case 'V':
            // Ignore auto-repeat so holding V doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0)
                ToggleVsync();
            return 0;

        default:
            break;
        }

        // Forward non-system keys to the input queue. The app/window layer does not
        // interpret them (it only does system-level toggles).
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            const bool isSystem = (vk == static_cast<std::uint32_t>(VK_ESCAPE)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F11)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F10)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F9)) ||
                                  (vk == static_cast<std::uint32_t>('V'));

            if (vk < 256 && !isSystem)
            {
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::KeyDown;
                ev.key = vk;
                ev.alt = (lParam & (1 << 29)) != 0;
                ev.repeat = (lParam & (1 << 30)) != 0;
                m_impl->input.Push(ev);
                return 0;
            }
        }
        break;
    }

    case WM_KEYUP:
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            const bool isSystem = (vk == static_cast<std::uint32_t>(VK_ESCAPE)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F11)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F10)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F9)) ||
                                  (vk == static_cast<std::uint32_t>('V'));

            if (vk < 256 && !isSystem)
            {
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::KeyUp;
                ev.key = vk;
                ev.alt = (lParam & (1 << 29)) != 0;
                ev.repeat = false;
                m_impl->input.Push(ev);
                return 0;
            }
        }
        break;

    case WM_SYSKEYDOWN:
    {
        // Alt+Enter (bit 29 = context code / Alt key down)
        // Ignore auto-repeat so holding Alt+Enter doesn't spam-toggle.
        if (wParam == VK_RETURN && (lParam & (1 << 29)) && ((lParam & (1 << 30)) == 0)) {
            ToggleFullscreen();
            return 0;
        }

        // Forward system keys (notably Alt) to the input queue so action-chords
        // like Alt+MouseLeft can be bound in InputMapper.
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            if (vk < 256)
            {
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::KeyDown;
                ev.key = vk;
                ev.alt = (lParam & (1 << 29)) != 0;
                ev.repeat = (lParam & (1 << 30)) != 0;
                m_impl->input.Push(ev);
            }

            // Prevent the classic Alt-key menu activation when using Alt as a modifier in-game.
            if (vk == static_cast<std::uint32_t>(VK_MENU) || vk == static_cast<std::uint32_t>(VK_LMENU) || vk == static_cast<std::uint32_t>(VK_RMENU))
            {
                return 0;
            }
        }

        // Let the system handle other Alt combos (Alt+F4, etc.).
        break;
    }

    case WM_SYSKEYUP:
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            if (vk < 256)
            {
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::KeyUp;
                ev.key = vk;
                ev.alt = (lParam & (1 << 29)) != 0;
                ev.repeat = false;
                m_impl->input.Push(ev);
            }

            if (vk == static_cast<std::uint32_t>(VK_MENU) || vk == static_cast<std::uint32_t>(VK_LMENU) || vk == static_cast<std::uint32_t>(VK_RMENU))
            {
                return 0;
            }
        }
        break;

    case WM_SYSCHAR:
        // Prevent the system beep on Alt+Enter.
        if (wParam == VK_RETURN && (lParam & (1 << 29))) {
            return 0;
        }
        break;

    // ---------------------------------------------------------------------
    // Mouse buttons & move (cursor deltas)
    // ---------------------------------------------------------------------
    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnLButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonLeft;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnLButtonUp(hWnd);

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonUp;
            bev.key  = colony::input::kMouseButtonLeft;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_RBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnRButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonRight;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_RBUTTONUP:
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnRButtonUp(hWnd);

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonUp;
            bev.key  = colony::input::kMouseButtonRight;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_MBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnMButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonMiddle;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_MBUTTONUP:
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            m_impl->mouse.OnMButtonUp(hWnd);

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonUp;
            bev.key  = colony::input::kMouseButtonMiddle;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_XBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            const WORD xb = GET_XBUTTON_WPARAM(wParam);
            m_impl->mouse.OnXButtonDown(hWnd,
                                        xb == XBUTTON1,
                                        GET_X_LPARAM(lParam),
                                        GET_Y_LPARAM(lParam));
            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = (xb == XBUTTON1) ? colony::input::kMouseButtonX1 : colony::input::kMouseButtonX2;
            m_impl->input.Push(bev);
        }
        return TRUE;

    case WM_XBUTTONUP:
        if (m_impl) {
            m_impl->FlushPendingMouseDelta();
            const WORD xb = GET_XBUTTON_WPARAM(wParam);
            m_impl->mouse.OnXButtonUp(hWnd, xb == XBUTTON1);
            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonUp;
            bev.key  = (xb == XBUTTON1) ? colony::input::kMouseButtonX1 : colony::input::kMouseButtonX2;
            m_impl->input.Push(bev);
        }
        return TRUE;

    case WM_MOUSEMOVE:
        if (m_impl)
        {
            LONG dx = 0;
            LONG dy = 0;
            if (m_impl->mouse.OnMouseMove(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), dx, dy))
            {
                // Aggregate per-frame to keep input stable on high polling
                // mice and avoid overflowing the fixed-size input queue.
                m_impl->pendingMouseDx += dx;
                m_impl->pendingMouseDy += dy;
            }
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (m_impl)
        {
            m_impl->FlushPendingMouseDelta();
            const int detents = m_impl->mouse.OnMouseWheel(wParam);
            colony::input::InputEvent ev{};
            ev.type = colony::input::InputEventType::MouseWheel;
            ev.wheelDetents = detents;
            m_impl->input.Push(ev);
        }
        return 0;

    // ---------------------------------------------------------------------
    // Raw input (high-resolution mouse deltas)
    // ---------------------------------------------------------------------
    case WM_INPUT:
        if (m_impl)
        {
            LONG dx = 0;
            LONG dy = 0;
            if (m_impl->mouse.OnRawInput(hWnd, reinterpret_cast<HRAWINPUT>(lParam), dx, dy))
            {
                // Aggregate per-frame to keep input stable on high polling
                // mice and avoid overflowing the fixed-size input queue.
                m_impl->pendingMouseDx += dx;
                m_impl->pendingMouseDy += dy;
            }
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
