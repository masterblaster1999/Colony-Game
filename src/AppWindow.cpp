#include "AppWindow.h"

#include "game/PrototypeGame.h"
#include "input/InputQueue.h"
#include "platform/win32/RawMouseInput.h"
#include "platform/win32/Win32Window.h"

#include "loop/FramePacer.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM

#include <string>

// ----------------------------------------------------------------------------
// AppWindow::Impl
// ----------------------------------------------------------------------------

struct AppWindow::Impl {
    colony::appwin::win32::RawMouseInput        mouse;
    colony::appwin::win32::BorderlessFullscreen fullscreen;
    colony::input::InputQueue                  input;
    colony::game::PrototypeGame                game;
    colony::appwin::FramePacer                 pacer;
};

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() = default;

// ----------------------------------------------------------------------------
// AppWindow
// ----------------------------------------------------------------------------

bool AppWindow::Create(HINSTANCE hInst, int nCmdShow, int width, int height)
{
    m_impl = std::make_unique<Impl>();

    const wchar_t* kClass = L"ColonyWindowClass";

    m_hwnd = colony::appwin::win32::CreateDpiAwareWindow(
        hInst,
        kClass,
        L"Colony Game",
        width,
        height,
        &AppWindow::WndProc,
        this
    );

    if (!m_hwnd)
        return false;

    // Snapshot initial windowed placement for fullscreen toggling.
    m_impl->fullscreen.InitFromCurrent(m_hwnd);

    // Enable WM_INPUT raw deltas (best-effort; falls back to cursor deltas).
    m_impl->mouse.Register(m_hwnd);

    RECT cr{};
    GetClientRect(m_hwnd, &cr);
    m_width  = static_cast<UINT>(cr.right);
    m_height = static_cast<UINT>(cr.bottom);

    if (!m_gfx.Init(m_hwnd, m_width, m_height))
        return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    UpdateTitle();
    return true;
}

void AppWindow::ToggleVsync()
{
    m_vsync = !m_vsync;
    UpdateTitle();
}

void AppWindow::ToggleFullscreen()
{
    if (!m_hwnd || !m_impl)
        return;

    m_impl->fullscreen.Toggle(m_hwnd);
    UpdateTitle();
}

void AppWindow::UpdateTitle()
{
    if (!m_hwnd || !m_impl)
        return;

    const wchar_t* vs = m_vsync ? L"ON" : L"OFF";
    const wchar_t* fs = m_impl->fullscreen.IsFullscreen() ? L"FULL" : L"WIN";
    const double fps = m_impl->pacer.Fps();

#ifndef NDEBUG
    const auto cam = m_impl->game.GetDebugCameraInfo();
    wchar_t title[256];
    swprintf_s(title,
               L"Colony Game | %.0f FPS | VSync %s | %s | yaw %.1f pitch %.1f pan(%.1f, %.1f) zoom %.2f",
               fps, vs, fs,
               cam.yaw, cam.pitch,
               cam.panX, cam.panY,
               cam.zoom);
    SetWindowTextW(m_hwnd, title);
#else
    wchar_t title[128];
    swprintf_s(title, L"Colony Game | %.0f FPS | VSync %s | %s", fps, vs, fs);
    SetWindowTextW(m_hwnd, title);
#endif
}

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
            m_impl->mouse.OnActivateApp(hWnd, active);
            if (!active) {
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::FocusLost;
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
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
    {
        const UINT w = LOWORD(lParam);
        const UINT h = HIWORD(lParam);
        m_width = w;
        m_height = h;

        if (w > 0 && h > 0)
            m_gfx.Resize(w, h);

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
            if (b.middle || b.right) { SetCursor(LoadCursor(nullptr, IDC_SIZEALL)); return TRUE; }
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
            ToggleFullscreen();
            return 0;

        case 'V':
            ToggleVsync();
            return 0;

        default:
            break;
        }

        // Forward gameplay keys to the input queue. The app/window layer does not
        // interpret them (it only does system-level toggles).
        if (m_impl)
        {
            const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
            if (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == 'Q' || vk == 'E')
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
            if (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == 'Q' || vk == 'E')
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
        // Alt+Enter (bit 29 = context code / Alt key down)
        if (wParam == VK_RETURN && (lParam & (1 << 29))) {
            ToggleFullscreen();
            return 0;
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
            m_impl->mouse.OnLButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_impl) m_impl->mouse.OnLButtonUp(hWnd);
        return 0;

    case WM_RBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->mouse.OnRButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_RBUTTONUP:
        if (m_impl) m_impl->mouse.OnRButtonUp(hWnd);
        return 0;

    case WM_MBUTTONDOWN:
        SetFocus(hWnd);
        if (m_impl) {
            m_impl->mouse.OnMButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_MBUTTONUP:
        if (m_impl) m_impl->mouse.OnMButtonUp(hWnd);
        return 0;

    case WM_MOUSEMOVE:
        if (m_impl)
        {
            LONG dx = 0;
            LONG dy = 0;
            if (m_impl->mouse.OnMouseMove(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), dx, dy))
            {
                const auto b = m_impl->mouse.Buttons();
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::MouseDelta;
                ev.dx = static_cast<std::int32_t>(dx);
                ev.dy = static_cast<std::int32_t>(dy);
                ev.buttons = 0;
                if (b.left)   ev.buttons |= colony::input::MouseButtonsMask::MouseLeft;
                if (b.right)  ev.buttons |= colony::input::MouseButtonsMask::MouseRight;
                if (b.middle) ev.buttons |= colony::input::MouseButtonsMask::MouseMiddle;
                m_impl->input.Push(ev);
            }
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (m_impl)
        {
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
                const auto b = m_impl->mouse.Buttons();
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::MouseDelta;
                ev.dx = static_cast<std::int32_t>(dx);
                ev.dy = static_cast<std::int32_t>(dy);
                ev.buttons = 0;
                if (b.left)   ev.buttons |= colony::input::MouseButtonsMask::MouseLeft;
                if (b.right)  ev.buttons |= colony::input::MouseButtonsMask::MouseRight;
                if (b.middle) ev.buttons |= colony::input::MouseButtonsMask::MouseMiddle;
                m_impl->input.Push(ev);
            }
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int AppWindow::MessageLoop()
{
    MSG msg{};

    if (!m_impl)
        m_impl = std::make_unique<Impl>();

    // Match previous behavior: schedule starts "unset" and FPS timing starts when
    // the loop begins.
    m_impl->pacer.ResetSchedule();
    m_impl->pacer.ResetFps();

    bool lastVsync = m_vsync;

    while (true)
    {
        // Reset pacing if vsync changes at runtime.
        if (lastVsync != m_vsync) {
            m_impl->pacer.ResetSchedule();
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

        // Frame pacing (vsync OFF): wait until either the next frame time arrives
        // or we receive input/messages.
        m_impl->pacer.ThrottleBeforeMessagePump(m_vsync);

        // Pump all queued messages.
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Apply input events to the game even if we end up skipping rendering
        // this iteration (e.g., vsync OFF + not time to render yet).
        if (m_impl)
        {
            const bool changed = m_impl->game.OnInput(m_impl->input.Events());
            m_impl->input.Clear();
            if (changed) {
                UpdateTitle();
            }
        }

        // Re-check minimized state after message pump.
        if (m_width == 0 || m_height == 0) {
            continue;
        }

        // If vsync is OFF and we woke due to messages, don't render early.
        if (!m_impl->pacer.IsTimeToRender(m_vsync)) {
            continue;
        }

        // Render one frame (your sim tick could go here too).
        m_gfx.Render(m_vsync);

        // FPS counter (update about once per second).
        if (m_impl->pacer.OnFramePresented(m_vsync)) {
            UpdateTitle();
        }
    }
}
