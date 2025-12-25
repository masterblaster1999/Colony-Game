#include "AppWindow.h"

#include "game/PrototypeGame.h"
#include "input/InputQueue.h"
#include "UserSettings.h"
#include "platform/win32/RawMouseInput.h"
#include "platform/win32/Win32Window.h"

#include "loop/FramePacer.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM

#include <string>
#include <chrono>

// ----------------------------------------------------------------------------
// AppWindow::Impl
// ----------------------------------------------------------------------------

struct AppWindow::Impl {
    colony::appwin::win32::RawMouseInput        mouse;
    colony::appwin::win32::BorderlessFullscreen fullscreen;
    colony::input::InputQueue                  input;
    colony::game::PrototypeGame                game;
    colony::appwin::FramePacer                 pacer;

    colony::appwin::UserSettings               settings;
    bool                                      settingsLoaded = false;
    bool                                      settingsDirty = false;

    // Debounced auto-save for settings writes.
    //
    // We avoid writing settings.json on every WM_SIZE during interactive resizing,
    // but we also don't want to lose changes if the app crashes.
    std::chrono::steady_clock::time_point      nextSettingsAutoSave{};
    bool                                      hasPendingAutoSave = false;

    // Window state
    bool                                      active = true;

    // When resizing via the window frame, defer swapchain resizes until the
    // user finishes the drag (WM_EXITSIZEMOVE). This avoids hammering
    // ResizeBuffers on every mouse move during sizing.
    bool                                      inSizeMove = false;
    UINT                                      pendingResizeW = 0;
    UINT                                      pendingResizeH = 0;
};

namespace {

using steady_clock = std::chrono::steady_clock;

// How long to wait after the *last* settings change before writing settings.json.
//
// Rationale:
//  - Avoids hammering the disk during window resizing (many WM_SIZE messages)
//  - Still persists toggles quickly enough to survive crashes
constexpr auto kSettingsAutoSaveDelay = std::chrono::milliseconds(750);

// If a write fails (e.g., transient AV scan/lock), back off before retrying.
constexpr auto kSettingsAutoSaveRetryDelay = std::chrono::seconds(2);

inline void ScheduleSettingsAutosave(AppWindow::Impl& impl) noexcept
{
    impl.settingsDirty = true;
    impl.hasPendingAutoSave = true;
    impl.nextSettingsAutoSave = steady_clock::now() + kSettingsAutoSaveDelay;
}

inline void MaybeAutoSaveSettings(AppWindow::Impl& impl) noexcept
{
    if (!impl.settingsDirty || !impl.hasPendingAutoSave)
        return;

    // Don't write mid-drag; wait for WM_EXITSIZEMOVE.
    if (impl.inSizeMove)
        return;

    const auto now = steady_clock::now();
    if (now < impl.nextSettingsAutoSave)
        return;

    if (colony::appwin::SaveUserSettings(impl.settings))
    {
        impl.settingsDirty = false;
        impl.hasPendingAutoSave = false;
        return;
    }

    // Retry later.
    impl.nextSettingsAutoSave = now + kSettingsAutoSaveRetryDelay;
    impl.hasPendingAutoSave = true;
}

inline DWORD BackgroundWaitTimeoutMs(const AppWindow::Impl& impl) noexcept
{
    if (!impl.settingsDirty || !impl.hasPendingAutoSave || impl.inSizeMove)
        return INFINITE;

    const auto now = steady_clock::now();
    if (now >= impl.nextSettingsAutoSave)
        return 0;

    // Clamp timeout to avoid overflow.
    const auto remaining = impl.nextSettingsAutoSave - now;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    if (ms <= 0)
        return 0;
    if (ms > 60'000)
        return 60'000;
    return static_cast<DWORD>(ms);
}

} // namespace

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() = default;

// ----------------------------------------------------------------------------
// AppWindow
// ----------------------------------------------------------------------------

bool AppWindow::Create(HINSTANCE hInst, int nCmdShow, int width, int height)
{
    m_impl = std::make_unique<Impl>();

    // Defaults (arguments win if no settings file exists yet).
    m_impl->settings.windowWidth = static_cast<std::uint32_t>(width > 0 ? width : 1280);
    m_impl->settings.windowHeight = static_cast<std::uint32_t>(height > 0 ? height : 720);
    m_impl->settings.vsync = m_vsync;
    m_impl->settings.fullscreen = false;
    m_impl->settings.maxFpsWhenVsyncOff = m_impl->pacer.MaxFpsWhenVsyncOff();
    m_impl->settings.pauseWhenUnfocused = true;
    m_impl->settings.maxFpsWhenUnfocused = m_impl->pacer.MaxFpsWhenUnfocused();

    // Best-effort load: if it fails, we keep the defaults above.
    {
        colony::appwin::UserSettings loaded = m_impl->settings;
        if (colony::appwin::LoadUserSettings(loaded))
        {
            m_impl->settings = loaded;
            m_impl->settingsLoaded = true;
        }
    }

    // Apply persisted settings.
    m_vsync = m_impl->settings.vsync;
    m_impl->pacer.SetMaxFpsWhenVsyncOff(m_impl->settings.maxFpsWhenVsyncOff);
    m_impl->pacer.SetMaxFpsWhenUnfocused(m_impl->settings.maxFpsWhenUnfocused);

    const wchar_t* kClass = L"ColonyWindowClass";

    m_hwnd = colony::appwin::win32::CreateDpiAwareWindow(
        hInst,
        kClass,
        L"Colony Game",
        static_cast<int>(m_impl->settings.windowWidth),
        static_cast<int>(m_impl->settings.windowHeight),
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

    // Apply initial fullscreen preference after the window is shown.
    if (m_impl->settings.fullscreen)
    {
        m_impl->fullscreen.Toggle(m_hwnd);

        // Ensure the swapchain matches the new client rect.
        RECT r{};
        GetClientRect(m_hwnd, &r);
        const UINT w = static_cast<UINT>(r.right);
        const UINT h = static_cast<UINT>(r.bottom);
        m_width = w;
        m_height = h;
        if (w > 0 && h > 0)
            m_gfx.Resize(w, h);
    }

    UpdateTitle();
    return true;
}

void AppWindow::ToggleVsync()
{
    m_vsync = !m_vsync;

    if (m_impl) {
        m_impl->settings.vsync = m_vsync;
        ScheduleSettingsAutosave(*m_impl);
    }

    UpdateTitle();
}

void AppWindow::ToggleFullscreen()
{
    if (!m_hwnd || !m_impl)
        return;

    m_impl->fullscreen.Toggle(m_hwnd);

    m_impl->settings.fullscreen = m_impl->fullscreen.IsFullscreen();
    ScheduleSettingsAutosave(*m_impl);

    UpdateTitle();
}

void AppWindow::TogglePauseWhenUnfocused()
{
    if (!m_impl)
        return;

    m_impl->settings.pauseWhenUnfocused = !m_impl->settings.pauseWhenUnfocused;
    ScheduleSettingsAutosave(*m_impl);
    UpdateTitle();
}

void AppWindow::UpdateTitle()
{
    if (!m_hwnd || !m_impl)
        return;

    const wchar_t* vs = m_vsync ? L"ON" : L"OFF";
    const wchar_t* fs = m_impl->fullscreen.IsFullscreen() ? L"FULL" : L"WIN";
    const wchar_t* act = m_impl->active
                             ? L"ACTIVE"
                             : (m_impl->settings.pauseWhenUnfocused ? L"BG (PAUSED)" : L"BG");
    const double fps = m_impl->pacer.Fps();

#ifndef NDEBUG
    const auto cam = m_impl->game.GetDebugCameraInfo();
    wchar_t title[256];
    swprintf_s(title,
               L"Colony Game | %.0f FPS | VSync %s | %s | %s | yaw %.1f pitch %.1f pan(%.1f, %.1f) zoom %.2f",
               fps, vs, fs, act,
               cam.yaw, cam.pitch,
               cam.panX, cam.panY,
               cam.zoom);
    SetWindowTextW(m_hwnd, title);
#else
    wchar_t title[128];
    swprintf_s(title, L"Colony Game | %.0f FPS | VSync %s | %s | %s", fps, vs, fs, act);
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
            m_impl->active = active;
            m_impl->mouse.OnActivateApp(hWnd, active);
            if (!active) {
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
            ScheduleSettingsAutosave(*m_impl);
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

        case VK_F9:
            // Ignore auto-repeat so holding F9 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0)
                TogglePauseWhenUnfocused();
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
                                  (vk == static_cast<std::uint32_t>(VK_F9))  ||
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
                                  (vk == static_cast<std::uint32_t>(VK_F9))  ||
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
            m_impl->mouse.OnLButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonLeft;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_impl) {
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
            m_impl->mouse.OnRButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonRight;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_RBUTTONUP:
        if (m_impl) {
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
            m_impl->mouse.OnMButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonMiddle;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_MBUTTONUP:
        if (m_impl) {
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
                const auto b = m_impl->mouse.Buttons();
                colony::input::InputEvent ev{};
                ev.type = colony::input::InputEventType::MouseDelta;
                ev.dx = static_cast<std::int32_t>(dx);
                ev.dy = static_cast<std::int32_t>(dy);
                ev.buttons = 0;
                if (b.left)   ev.buttons |= colony::input::MouseButtonsMask::MouseLeft;
                if (b.right)  ev.buttons |= colony::input::MouseButtonsMask::MouseRight;
                if (b.middle) ev.buttons |= colony::input::MouseButtonsMask::MouseMiddle;
                if (b.x1)     ev.buttons |= colony::input::MouseButtonsMask::MouseX1;
                if (b.x2)     ev.buttons |= colony::input::MouseButtonsMask::MouseX2;
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
                if (b.x1)     ev.buttons |= colony::input::MouseButtonsMask::MouseX1;
                if (b.x2)     ev.buttons |= colony::input::MouseButtonsMask::MouseX2;
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
    bool lastUnfocused = m_impl ? !m_impl->active : false;

    while (true)
    {
        const bool unfocused = m_impl ? !m_impl->active : false;
        const bool pauseInBackground = m_impl ? (unfocused && m_impl->settings.pauseWhenUnfocused) : false;

        // Reset pacing when the pacing mode changes (vsync toggled, or we moved
        // between foreground/background). This prevents long sleeps after e.g.
        // Alt+Tab.
        if (lastVsync != m_vsync || lastUnfocused != unfocused) {
            m_impl->pacer.ResetSchedule();
            lastVsync = m_vsync;
            lastUnfocused = unfocused;
        }

        // If minimized or intentionally paused in the background, don't render;
        // block until something happens. We still consume any queued input events
        // so FocusLost (etc.) reaches the game layer.
        if (m_width == 0 || m_height == 0 || pauseInBackground)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (m_impl)
            {
                const bool changed = m_impl->game.OnInput(m_impl->input.Events());
                m_impl->input.Clear();
                if (changed)
                    UpdateTitle();

                // Persist any queued settings changes while we're idle/minimized.
                MaybeAutoSaveSettings(*m_impl);
            }

            // If we have a pending settings auto-save, wake up in time to write it.
            const DWORD timeoutMs = m_impl ? BackgroundWaitTimeoutMs(*m_impl) : INFINITE;
            MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        // Frame pacing: wait until either the next frame time arrives (when a
        // cap is active) or we receive input/messages.
        m_impl->pacer.ThrottleBeforeMessagePump(m_vsync, unfocused);

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

            // Debounced settings persistence (non-blocking).
            MaybeAutoSaveSettings(*m_impl);
        }

        // Re-check minimized state after message pump.
        const bool unfocusedAfterPump = m_impl ? !m_impl->active : false;
        const bool pauseInBackgroundAfterPump = m_impl ? (unfocusedAfterPump && m_impl->settings.pauseWhenUnfocused) : false;
        if (m_width == 0 || m_height == 0 || pauseInBackgroundAfterPump) {
            continue;
        }

        // If a cap is active and we woke due to messages, don't render early.
        if (!m_impl->pacer.IsTimeToRender(m_vsync, unfocusedAfterPump)) {
            continue;
        }

        // Render one frame (your sim tick could go here too).
        m_gfx.Render(m_vsync);

        // FPS counter (update about once per second).
        if (m_impl->pacer.OnFramePresented(m_vsync, unfocusedAfterPump)) {
            UpdateTitle();
        }
    }
}
