#include "AppWindow_Internal.h"

#include "core/Log.h"

#include <windowsx.h>

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }

    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
        return self->HandleMsg(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Feed ImGui first so it can update WantCaptureMouse/Keyboard state.
    bool imguiConsumed = false;
    bool uiWantsMouse = false;
    bool uiWantsKeyboard = false;
    bool uiWantsText = false;

#if defined(COLONY_WITH_IMGUI)
    const bool uiActive = m_impl && m_impl->imguiInitialized && m_impl->overlayVisible && m_impl->imgui.enabled;
    if (uiActive)
    {
        imguiConsumed = m_impl->imgui.handleWndProc(hwnd, msg, wParam, lParam);
        uiWantsMouse = m_impl->imgui.wantsMouse();
        uiWantsKeyboard = m_impl->imgui.wantsKeyboard();
        uiWantsText = m_impl->imgui.wantsTextInput();
    }
#endif

    switch (msg)
    {
        case WM_DESTROY:
        {
            if (m_impl)
            {
                // Persist the latest settings even if we didn't get a chance to
                // run the debounced autosave.
                m_impl->settings.windowWidth = static_cast<int>(m_width);
                m_impl->settings.windowHeight = static_cast<int>(m_height);
                m_impl->settings.vsync = m_vsync;
                m_impl->settings.fullscreen = m_impl->fullscreen.IsFullscreen();

                colony::appwin::SaveUserSettings(m_impl->settings);

#if defined(COLONY_WITH_IMGUI)
                if (m_impl->imguiInitialized)
                {
                    m_impl->imgui.shutdown();
                    m_impl->imguiInitialized = false;
                }
#endif
            }

            PostQuitMessage(0);
            return 0;
        }

        case WM_ENTERSIZEMOVE:
        {
            if (m_impl)
                m_impl->inSizeMove = true;
            return 0;
        }

        case WM_EXITSIZEMOVE:
        {
            if (m_impl)
            {
                m_impl->inSizeMove = false;
                if (m_impl->pendingResizeW != 0 && m_impl->pendingResizeH != 0)
                {
                    m_width = m_impl->pendingResizeW;
                    m_height = m_impl->pendingResizeH;
                    m_gfx.Resize(m_width, m_height);

                    // Only persist a windowed size.
                    if (!m_impl->fullscreen.IsFullscreen())
                    {
                        m_impl->settings.windowWidth = static_cast<int>(m_width);
                        m_impl->settings.windowHeight = static_cast<int>(m_height);
                        MarkSettingsDirty();
                    }

                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::WindowResize;
                    ev.resize.width = m_width;
                    ev.resize.height = m_height;
                    m_impl->input.Push(ev);

                    m_impl->pendingResizeW = 0;
                    m_impl->pendingResizeH = 0;
                }
            }
            return 0;
        }

        case WM_SIZE:
        {
            const UINT w = LOWORD(lParam);
            const UINT h = HIWORD(lParam);

            if (wParam == SIZE_MINIMIZED)
            {
                m_width = w;
                m_height = h;
                return 0;
            }

            if (w == 0 || h == 0)
                return 0;

            m_width = w;
            m_height = h;

            if (m_impl && m_impl->inSizeMove)
            {
                m_impl->pendingResizeW = w;
                m_impl->pendingResizeH = h;
                return 0;
            }

            m_gfx.Resize(w, h);

            if (m_impl)
            {
                // Only persist a windowed size.
                if (!m_impl->fullscreen.IsFullscreen())
                {
                    m_impl->settings.windowWidth = static_cast<int>(w);
                    m_impl->settings.windowHeight = static_cast<int>(h);
                    MarkSettingsDirty();
                }

                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::WindowResize;
                ev.resize.width = w;
                ev.resize.height = h;
                m_impl->input.Push(ev);
            }

            return 0;
        }

        case WM_ACTIVATEAPP:
        {
            if (m_impl)
            {
                m_impl->active = (wParam != FALSE);

                if (wParam == FALSE)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::FocusLost;
                    m_impl->input.Push(ev);
                }

                UpdateTitle();
            }
            return 0;
        }

        case WM_SYSKEYDOWN:
        {
            const bool isRepeat = (lParam & (1 << 30)) != 0;
            if (!isRepeat && wParam == VK_RETURN && (lParam & (1 << 29)))
            {
                ToggleFullscreen();
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
        {
            const bool isRepeat = (lParam & (1 << 30)) != 0;
            if (isRepeat)
                break;

            if (wParam == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 0;
            }

            if (wParam == VK_F1)
            {
                ToggleOverlay();
                return 0;
            }

            if (wParam == VK_F11)
            {
                ToggleFullscreen();
                return 0;
            }

            if (wParam == 'V')
            {
                // Avoid stealing "V" from ImGui text inputs.
                if (!uiWantsText)
                    ToggleVsync();
                return 0;
            }

            break;
        }

        case WM_INPUT:
        {
            if (m_impl)
            {
                LONG dx = 0, dy = 0;
                if (m_impl->mouse.OnRawInput(hwnd, lParam, dx, dy))
                {
                    if (!uiWantsMouse)
                    {
                        colony::input::InputEvent ev{};
                        ev.type = colony::input::InputEventType::MouseDelta;
                        ev.mouseDelta.dx = dx;
                        ev.mouseDelta.dy = dy;
                        m_impl->input.Push(ev);
                    }
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnMouseMove(x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseMove;
                    ev.mouseMove.x = x;
                    ev.mouseMove.y = y;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnLButtonDown(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonDown;
                    ev.mouseButtonDown.button = colony::input::MouseButton::Left;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnLButtonUp(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonUp;
                    ev.mouseButtonUp.button = colony::input::MouseButton::Left;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnRButtonDown(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonDown;
                    ev.mouseButtonDown.button = colony::input::MouseButton::Right;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_RBUTTONUP:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnRButtonUp(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonUp;
                    ev.mouseButtonUp.button = colony::input::MouseButton::Right;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_MBUTTONDOWN:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnMButtonDown(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonDown;
                    ev.mouseButtonDown.button = colony::input::MouseButton::Middle;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_MBUTTONUP:
        {
            if (m_impl)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                m_impl->mouse.OnMButtonUp(hwnd, x, y);

                if (!uiWantsMouse)
                {
                    colony::input::InputEvent ev{};
                    ev.type = colony::input::InputEventType::MouseButtonUp;
                    ev.mouseButtonUp.button = colony::input::MouseButton::Middle;
                    m_impl->input.Push(ev);
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (m_impl && !uiWantsMouse)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::MouseWheel;
                ev.mouseWheel.delta = delta;
                m_impl->input.Push(ev);
            }
            return 0;
        }

        case WM_SETCURSOR:
        {
            if (m_impl)
            {
                // If ImGui is actively capturing mouse, let its Win32 backend
                // drive the cursor shape (resize, text input, etc.).
                if (imguiConsumed)
                    return TRUE;

                const auto cur = m_impl->mouse.DesiredCursor();
                SetCursor(LoadCursorW(nullptr, cur));
                return TRUE;
            }
            break;
        }

        default:
            break;
    }

    if (imguiConsumed)
        return 0;

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
