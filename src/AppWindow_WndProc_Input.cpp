#include "AppWindow_Impl.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM

#include <cstdint>

namespace {

// Translate generic VK_* modifier codes (VK_SHIFT/VK_CONTROL/VK_MENU) into left/right
// variants using lParam scan code / extended bit.
// Returns vk unchanged for non-modifier keys.
[[nodiscard]] std::uint32_t TranslateModifierVk(std::uint32_t vk, LPARAM lParam) noexcept
{
    if (vk == static_cast<std::uint32_t>(VK_SHIFT))
    {
        // For Shift, the extended-bit doesn't distinguish left/right. Use the scan code.
        const UINT sc = (static_cast<UINT>((lParam & 0x00ff0000) >> 16));
        const UINT vkEx = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
        if (vkEx == VK_LSHIFT || vkEx == VK_RSHIFT)
            return static_cast<std::uint32_t>(vkEx);
        return vk;
    }

    if (vk == static_cast<std::uint32_t>(VK_CONTROL))
    {
        // Extended bit (bit 24) is set for the right-side key.
        return (lParam & 0x01000000) ? static_cast<std::uint32_t>(VK_RCONTROL)
                                     : static_cast<std::uint32_t>(VK_LCONTROL);
    }

    if (vk == static_cast<std::uint32_t>(VK_MENU))
    {
        // Extended bit (bit 24) is set for the right-side key.
        return (lParam & 0x01000000) ? static_cast<std::uint32_t>(VK_RMENU)
                                     : static_cast<std::uint32_t>(VK_LMENU);
    }

    return vk;
}

inline void PushKeyEventDual(colony::input::InputQueue& q,
                             colony::input::InputEventType type,
                             std::uint32_t vk,
                             std::uint32_t vkSpecific,
                             bool alt,
                             bool repeat) noexcept
{
    colony::input::InputEvent ev{};
    ev.type = type;
    ev.key = vk;
    ev.alt = alt;
    ev.repeat = repeat;
    q.Push(ev);

    // Also emit the left/right variant (useful for explicit LShift/RShift bindings).
    if (vkSpecific != vk)
    {
        ev.key = vkSpecific;
        q.Push(ev);
    }
}

} // namespace

// -------------------------------------------------------------------------------------------------
// AppWindow message handling: Input (keyboard / mouse / raw input)
// -------------------------------------------------------------------------------------------------

LRESULT AppWindow::HandleMsg_Input(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    switch (msg)
    {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_impl) {
            // Cache cursors to avoid repeated LoadCursor calls (WM_SETCURSOR can fire a lot).
            static HCURSOR s_sizeAll = LoadCursor(nullptr, IDC_SIZEALL);
            static HCURSOR s_hand    = LoadCursor(nullptr, IDC_HAND);

            const auto b = m_impl->mouse.Buttons();
            if (b.middle || b.right || b.x1 || b.x2) { SetCursor(s_sizeAll); handled = true; return TRUE; }
            if (b.left)                              { SetCursor(s_hand);    handled = true; return TRUE; }
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
            handled = true;
            return 0;

        case VK_F1:
            // Toggle gameplay panels (Build/Colony/Help) without relying on ImGui focus.
            // Ignore auto-repeat so holding F1 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
                m_impl->game.TogglePanels();
            handled = true;
            return 0;

        case VK_F2:
            // Toggle help/controls overlay.
            // Ignore auto-repeat so holding F2 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
                m_impl->game.ToggleHelp();
            handled = true;
            return 0;

        case VK_F3:
            // Show runtime hotkeys (MessageBox). Useful even without ImGui.
            // Ignore auto-repeat so holding F3 doesn't spam.
            if ((lParam & (1 << 30)) == 0)
                ShowHotkeysHelp();
            handled = true;
            return 0;

        case VK_F11:
            // Ignore auto-repeat so holding F11 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0)
                ToggleFullscreen();
            handled = true;
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
            handled = true;
            return 0;

        case VK_F12:
            // Toggle DXGI diagnostics in the title bar.
            // Ignore auto-repeat so holding F12 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                m_impl->settings.showDxgiDiagnostics = !m_impl->settings.showDxgiDiagnostics;
                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            handled = true;
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
            handled = true;
            return 0;

        case VK_F8:
            // Cycle DXGI maximum frame latency (1..16).
            // Lower values reduce input latency; higher values can improve throughput.
            // Ignore auto-repeat so holding F8 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                int v = m_impl->settings.maxFrameLatency;
                if (v < 1) v = 1;
                v = (v >= 16) ? 1 : (v + 1);

                m_impl->settings.maxFrameLatency = v;
                m_gfx.SetMaxFrameLatency(static_cast<UINT>(v));

                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            handled = true;
            return 0;

        case VK_F7:
            // Toggle pausing behavior when the window is unfocused.
            // Ignore auto-repeat so holding F7 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                m_impl->settings.pauseWhenUnfocused = !m_impl->settings.pauseWhenUnfocused;
                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            handled = true;
            return 0;

        case VK_F6:
            // Cycle FPS caps.
            //  - F6:        cap when VSync is OFF
            //  - Shift+F6:   cap when unfocused (only matters when pauseWhenUnfocused is false)
            // Ignore auto-repeat so holding F6 doesn't spam.
            if ((lParam & (1 << 30)) == 0)
            {
                const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shiftDown)
                    CycleMaxFpsWhenUnfocused();
                else
                    CycleMaxFpsWhenVsyncOff();
            }
            handled = true;
            return 0;

        case 'V':
            // Ignore auto-repeat so holding V doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0)
                ToggleVsync();
            handled = true;
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
                                  (vk == static_cast<std::uint32_t>(VK_F12)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F9)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F8)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F7)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F6)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F3)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F1)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F2)) ||
                                  (vk == static_cast<std::uint32_t>('V'));

            if (vk < 256 && !isSystem)
            {
                const std::uint32_t vkSpecific = TranslateModifierVk(vk, lParam);
                const bool alt = (lParam & (1 << 29)) != 0;
                const bool repeat = (lParam & (1 << 30)) != 0;
                PushKeyEventDual(m_impl->input, colony::input::InputEventType::KeyDown, vk, vkSpecific, alt, repeat);
                handled = true;
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
                                  (vk == static_cast<std::uint32_t>(VK_F1)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F2)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F3)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F6)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F11)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F10)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F12)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F9)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F8)) ||
                                  (vk == static_cast<std::uint32_t>(VK_F7)) ||
                                  (vk == static_cast<std::uint32_t>('V'));

            if (vk < 256 && !isSystem)
            {
                const std::uint32_t vkSpecific = TranslateModifierVk(vk, lParam);
                const bool alt = (lParam & (1 << 29)) != 0;
                PushKeyEventDual(m_impl->input, colony::input::InputEventType::KeyUp, vk, vkSpecific, alt, false);
                handled = true;
                return 0;
            }
        }
        break;

    case WM_SYSKEYDOWN:
    {
        // F10 is treated as a system key by Win32 (WM_SYSKEYDOWN).
        // Mirror the WM_KEYDOWN handler so the toggle works consistently.
        if (wParam == VK_F10)
        {
            if ((lParam & (1 << 30)) == 0 && m_impl) {
                m_impl->settings.showFrameStats = !m_impl->settings.showFrameStats;
                m_impl->settings.showDxgiDiagnostics = false;
                m_impl->ScheduleSettingsAutosave();
                UpdateTitle();
            }
            handled = true;
            return 0;
        }

        // Alt+Enter (bit 29 = context code / Alt key down)
        // Ignore auto-repeat so holding Alt+Enter doesn't spam-toggle.
        if (wParam == VK_RETURN && (lParam & (1 << 29)) && ((lParam & (1 << 30)) == 0)) {
            ToggleFullscreen();
            handled = true;
            return 0;
        }

        // Forward system keys (notably Alt) to the input queue so action-chords
        // like Alt+MouseLeft can be bound in InputMapper.
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            if (vk < 256)
            {
                const std::uint32_t vkSpecific = TranslateModifierVk(vk, lParam);
                const bool alt = (lParam & (1 << 29)) != 0;
                const bool repeat = (lParam & (1 << 30)) != 0;
                PushKeyEventDual(m_impl->input, colony::input::InputEventType::KeyDown, vk, vkSpecific, alt, repeat);
            }

            // Prevent the classic Alt-key menu activation when using Alt as a modifier in-game.
            if (vk == static_cast<std::uint32_t>(VK_MENU) || vk == static_cast<std::uint32_t>(VK_LMENU) || vk == static_cast<std::uint32_t>(VK_RMENU))
            {
                handled = true;
                return 0;
            }
        }

        // Let the system handle other Alt combos (Alt+F4, etc.).
        break;
    }

    case WM_SYSKEYUP:
        // If F10 was consumed by the window layer (stats toggle), don't forward it as gameplay input.
        if (wParam == VK_F10) { handled = true; return 0; }
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            if (vk < 256)
            {
                const std::uint32_t vkSpecific = TranslateModifierVk(vk, lParam);
                const bool alt = (lParam & (1 << 29)) != 0;
                PushKeyEventDual(m_impl->input, colony::input::InputEventType::KeyUp, vk, vkSpecific, alt, false);
            }
        }
        break;

    case WM_SYSCHAR:
        // Prevent the system beep on Alt+Enter.
        if (wParam == VK_RETURN && (lParam & (1 << 29))) {
            handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        // Per Win32 docs, WM_INPUT should be passed to DefWindowProc so the system can
        // perform internal cleanup. We still return 0 to indicate we processed it.
        (void)DefWindowProcW(hWnd, msg, wParam, lParam);
        handled = true;
        return 0;

    default:
        break;
    }

    handled = false;
    return 0;
}
