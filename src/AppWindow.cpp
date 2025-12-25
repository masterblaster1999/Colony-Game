#include "AppWindow.h"

#include "game/PrototypeGame.h"
#include "input/InputQueue.h"
#include "UserSettings.h"
#include "platform/win32/RawMouseInput.h"
#include "platform/win32/Win32Window.h"

#include "loop/FramePacer.h"
#include "loop/FramePacingStats.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
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
    colony::appwin::FramePacingStats           frameStats;

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

    // Mouse delta aggregation (prevents InputQueue overflow with very high
    // polling rate mice; flushed into a single MouseDelta event per pump).
    long long                                 pendingMouseDx = 0;
    long long                                 pendingMouseDy = 0;
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

inline std::int32_t ClampI32(long long v) noexcept
{
    if (v < static_cast<long long>(std::numeric_limits<std::int32_t>::min()))
        return std::numeric_limits<std::int32_t>::min();
    if (v > static_cast<long long>(std::numeric_limits<std::int32_t>::max()))
        return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(v);
}

inline void FlushPendingMouseDelta(AppWindow::Impl& impl) noexcept
{
    if (impl.pendingMouseDx == 0 && impl.pendingMouseDy == 0)
        return;

    const auto b = impl.mouse.Buttons();

    colony::input::InputEvent ev{};
    ev.type = colony::input::InputEventType::MouseDelta;
    ev.dx = ClampI32(impl.pendingMouseDx);
    ev.dy = ClampI32(impl.pendingMouseDy);
    ev.buttons = 0;
    if (b.left)   ev.buttons |= colony::input::MouseButtonsMask::MouseLeft;
    if (b.right)  ev.buttons |= colony::input::MouseButtonsMask::MouseRight;
    if (b.middle) ev.buttons |= colony::input::MouseButtonsMask::MouseMiddle;
    if (b.x1)     ev.buttons |= colony::input::MouseButtonsMask::MouseX1;
    if (b.x2)     ev.buttons |= colony::input::MouseButtonsMask::MouseX2;

    impl.input.Push(ev);
    impl.pendingMouseDx = 0;
    impl.pendingMouseDy = 0;
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
    m_impl->settings.maxFrameLatency = 1; // lowest latency by default
    m_impl->settings.swapchainScaling = colony::appwin::SwapchainScalingMode::None;
    m_impl->settings.pauseWhenUnfocused = true;
    m_impl->settings.maxFpsWhenUnfocused = m_impl->pacer.MaxFpsWhenUnfocused();
    m_impl->settings.rawMouse = true;
    m_impl->settings.showFrameStats = false;

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
    if (m_impl->settings.rawMouse)
        m_impl->mouse.Register(m_hwnd);

    RECT cr{};
    GetClientRect(m_hwnd, &cr);
    m_width  = static_cast<UINT>(cr.right);
    m_height = static_cast<UINT>(cr.bottom);

    DxDeviceOptions gfxOpt{};
    gfxOpt.maxFrameLatency = static_cast<UINT>(m_impl->settings.maxFrameLatency);
    gfxOpt.enableWaitableObject = true;
    switch (m_impl->settings.swapchainScaling)
    {
    case colony::appwin::SwapchainScalingMode::Stretch: gfxOpt.scaling = DXGI_SCALING_STRETCH; break;
    case colony::appwin::SwapchainScalingMode::Aspect:  gfxOpt.scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH; break;
    case colony::appwin::SwapchainScalingMode::None:
    default:                                           gfxOpt.scaling = DXGI_SCALING_NONE; break;
    }

    if (!m_gfx.Init(m_hwnd, m_width, m_height, gfxOpt))
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

    std::wostringstream oss;
    oss.setf(std::ios::fixed);
    oss << L"Colony Game | " << std::setprecision(0) << fps << L" FPS"
        << L" | VSync " << vs
        << L" | " << fs
        << L" | " << act;

    if (m_impl->settings.showFrameStats)
    {
        oss << L" | " << m_impl->frameStats.FormatTitleString();
    }

#ifndef NDEBUG
    const auto cam = m_impl->game.GetDebugCameraInfo();
    oss << L" | yaw " << std::setprecision(1) << cam.yaw
        << L" pitch " << cam.pitch
        << L" pan(" << cam.panX << L", " << cam.panY << L")"
        << L" zoom " << std::setprecision(2) << cam.zoom;
#endif

    const std::wstring title = oss.str();
    SetWindowTextW(m_hwnd, title.c_str());
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

        case VK_F10:
            // Toggle title-bar frame pacing stats (PresentMon-style summary)
            // Ignore auto-repeat so holding F10 doesn't spam-toggle.
            if ((lParam & (1 << 30)) == 0 && m_impl)
            {
                m_impl->settings.showFrameStats = !m_impl->settings.showFrameStats;
                m_impl->frameStats.Reset();
                ScheduleSettingsAutosave(*m_impl);
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

                ScheduleSettingsAutosave(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
            m_impl->mouse.OnLButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonLeft;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_impl) {
            FlushPendingMouseDelta(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
            m_impl->mouse.OnRButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonRight;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_RBUTTONUP:
        if (m_impl) {
            FlushPendingMouseDelta(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
            m_impl->mouse.OnMButtonDown(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

            colony::input::InputEvent bev{};
            bev.type = colony::input::InputEventType::MouseButtonDown;
            bev.key  = colony::input::kMouseButtonMiddle;
            m_impl->input.Push(bev);
        }
        return 0;

    case WM_MBUTTONUP:
        if (m_impl) {
            FlushPendingMouseDelta(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
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
            FlushPendingMouseDelta(*m_impl);
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

int AppWindow::MessageLoop()
{
    MSG msg{};

    if (!m_impl)
        m_impl = std::make_unique<Impl>();

    // Match previous behavior: schedule starts "unset" and FPS timing starts when
    // the loop begins.
    m_impl->pacer.ResetSchedule();
    m_impl->pacer.ResetFps();
    m_impl->frameStats.Reset();

    bool lastVsync = m_vsync;
    bool lastUnfocused = !m_impl->active;

    auto lastPresented = std::chrono::steady_clock::now();

    while (true)
    {
        const bool unfocused = !m_impl->active;
        const bool pauseInBackground = unfocused && m_impl->settings.pauseWhenUnfocused;

        // Reset pacing when the pacing mode changes (vsync toggled, or we moved
        // between foreground/background). This prevents long sleeps after e.g.
        // Alt+Tab.
        if (lastVsync != m_vsync || lastUnfocused != unfocused)
        {
            m_impl->pacer.ResetSchedule();
            lastVsync = m_vsync;
            lastUnfocused = unfocused;
        }

        // If minimized or intentionally paused in the background, don't render;
        // block until something happens. We still consume queued input events
        // so FocusLost (etc.) reaches the game layer.
        if (m_width == 0 || m_height == 0 || pauseInBackground)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Flush any buffered mouse delta into the queue before we hand it to the game.
            FlushPendingMouseDelta(*m_impl);

            const bool changed = m_impl->game.OnInput(m_impl->input.Events());
            m_impl->input.Clear();
            if (changed)
                UpdateTitle();

            // Persist any queued settings changes while we're idle/minimized.
            MaybeAutoSaveSettings(*m_impl);

            // If we have a pending settings auto-save, wake up in time to write it.
            const DWORD timeoutMs = BackgroundWaitTimeoutMs(*m_impl);
            MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        // Frame pacing: wait until either the next frame time arrives (when a
        // cap is active) or we receive input/messages.
        m_impl->pacer.ThrottleBeforeMessagePump(m_vsync, unfocused);

        // Pump all queued messages.
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Flush aggregated mouse movement after consuming the current burst of messages.
        FlushPendingMouseDelta(*m_impl);

        const bool unfocusedAfterPump = !m_impl->active;
        const bool pauseInBackgroundAfterPump = unfocusedAfterPump && m_impl->settings.pauseWhenUnfocused;
        if (m_width == 0 || m_height == 0 || pauseInBackgroundAfterPump)
            continue;

        // If a cap is active and we woke due to messages, don't render early.
        if (!m_impl->pacer.IsTimeToRender(m_vsync, unfocusedAfterPump))
        {
            const bool changed = m_impl->game.OnInput(m_impl->input.Events());
            m_impl->input.Clear();
            if (changed)
                UpdateTitle();

            // Debounced settings persistence (non-blocking).
            MaybeAutoSaveSettings(*m_impl);
            continue;
        }

        // If the swapchain exposes a frame-latency waitable object, block on it
        // (while still pumping messages) to avoid queuing ahead.
        double waitMs = 0.0;
        const HANDLE frameLatency = m_gfx.FrameLatencyWaitableObject();
        if (frameLatency != nullptr)
        {
            const auto waitStart = std::chrono::steady_clock::now();
            bool abortFrame = false;

            while (true)
            {
                const DWORD timeoutMs = BackgroundWaitTimeoutMs(*m_impl);
                const DWORD r = MsgWaitForMultipleObjectsEx(1, &frameLatency, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

                if (r == WAIT_OBJECT_0)
                {
                    // Frame slot available.
                    break;
                }

                if (r == WAIT_OBJECT_0 + 1)
                {
                    // Windows messages pending.
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (msg.message == WM_QUIT)
                            return static_cast<int>(msg.wParam);
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }

                    FlushPendingMouseDelta(*m_impl);

                    const bool unfocusedNow = !m_impl->active;
                    const bool pauseNow = unfocusedNow && m_impl->settings.pauseWhenUnfocused;

                    // State changed while waiting; restart the loop.
                    if (m_width == 0 || m_height == 0 || pauseNow)
                    {
                        abortFrame = true;
                        break;
                    }

                    // If a cap is active, ensure we still respect it.
                    if (!m_impl->pacer.IsTimeToRender(m_vsync, unfocusedNow))
                    {
                        abortFrame = true;
                        break;
                    }

                    continue;
                }

                if (r == WAIT_TIMEOUT)
                {
                    MaybeAutoSaveSettings(*m_impl);
                    continue;
                }

                // WAIT_FAILED or unexpected return; don't hang.
                break;
            }

            waitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();

            if (abortFrame)
            {
                const bool changed = m_impl->game.OnInput(m_impl->input.Events());
                m_impl->input.Clear();
                if (changed)
                    UpdateTitle();
                MaybeAutoSaveSettings(*m_impl);
                continue;
            }
        }

        // Apply input to the game as close to Present() as possible (lower latency).
        const bool changed = m_impl->game.OnInput(m_impl->input.Events());
        m_impl->input.Clear();
        if (changed)
            UpdateTitle();

        MaybeAutoSaveSettings(*m_impl);

        // Render one frame.
        const DxRenderStats rs = m_gfx.Render(m_vsync);
        const auto afterRender = std::chrono::steady_clock::now();

        // If DXGI reports occlusion, avoid burning CPU/GPU. We'll yield a bit and retry.
        if (rs.occluded)
        {
            m_impl->pacer.ResetSchedule();
            MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        const double frameMs = std::chrono::duration<double, std::milli>(afterRender - lastPresented).count();
        lastPresented = afterRender;

        // PresentMon-style rolling stats (computed a few times a second).
        m_impl->frameStats.AddSample(frameMs, rs.presentMs, waitMs);
        const bool statsUpdated = m_impl->frameStats.Update(afterRender);

        // FPS counter (update about once per second).
        const bool fpsTick = m_impl->pacer.OnFramePresented(m_vsync, unfocusedAfterPump);

        if (fpsTick || (m_impl->settings.showFrameStats && statsUpdated))
            UpdateTitle();
    }
}
