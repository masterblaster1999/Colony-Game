#include "AppWindow_Impl.h"

#include "DxDevice.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

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
        m_impl->ScheduleSettingsAutosave();
    }

    UpdateTitle();
}

void AppWindow::ToggleFullscreen()
{
    if (!m_hwnd || !m_impl)
        return;

    m_impl->fullscreen.Toggle(m_hwnd);

    m_impl->settings.fullscreen = m_impl->fullscreen.IsFullscreen();
    m_impl->ScheduleSettingsAutosave();

    UpdateTitle();
}


void AppWindow::CycleMaxFpsWhenVsyncOff()
{
    if (!m_impl)
        return;

    // Common monitor refresh caps + "unlimited" (0).
    constexpr std::array<std::uint32_t, 6> kCaps{ { 0u, 60u, 120u, 144u, 165u, 240u } };

    const std::uint32_t cur = m_impl->pacer.MaxFpsWhenVsyncOff();

    // If the current value isn't in the list (e.g. edited in JSON), pick the next
    // higher cap; otherwise, cycle to the next element.
    bool matched = false;
    std::uint32_t next = kCaps[0];

    for (std::size_t i = 0; i < kCaps.size(); ++i)
    {
        if (kCaps[i] == cur)
        {
            next = kCaps[(i + 1) % kCaps.size()];
            matched = true;
            break;
        }
    }

    if (!matched)
    {
        // Skip index 0 (unlimited) for "next higher cap" selection.
        next = 0;
        for (std::size_t i = 1; i < kCaps.size(); ++i)
        {
            if (kCaps[i] > cur)
            {
                next = kCaps[i];
                break;
            }
        }
    }

    m_impl->pacer.SetMaxFpsWhenVsyncOff(next);

    m_impl->settings.maxFpsWhenVsyncOff = next;
    m_impl->ScheduleSettingsAutosave();

    UpdateTitle();
}

void AppWindow::CycleMaxFpsWhenUnfocused()
{
    if (!m_impl)
        return;

    // Background caps: keep CPU usage down when alt-tabbed / unfocused.
    constexpr std::array<std::uint32_t, 5> kCaps{ { 0u, 5u, 10u, 30u, 60u } };

    const std::uint32_t cur = m_impl->pacer.MaxFpsWhenUnfocused();

    bool matched = false;
    std::uint32_t next = kCaps[0];

    for (std::size_t i = 0; i < kCaps.size(); ++i)
    {
        if (kCaps[i] == cur)
        {
            next = kCaps[(i + 1) % kCaps.size()];
            matched = true;
            break;
        }
    }

    if (!matched)
    {
        next = 0;
        for (std::size_t i = 1; i < kCaps.size(); ++i)
        {
            if (kCaps[i] > cur)
            {
                next = kCaps[i];
                break;
            }
        }
    }

    m_impl->pacer.SetMaxFpsWhenUnfocused(next);

    m_impl->settings.maxFpsWhenUnfocused = next;
    m_impl->ScheduleSettingsAutosave();

    UpdateTitle();
}

void AppWindow::ShowHotkeysHelp()
{
    const auto CapStr = [](std::uint32_t v) -> std::wstring {
        if (v == 0) return L"∞";
        return std::to_wstring(v);
    };

    std::wostringstream oss;
    oss << L"Hotkeys\r\n"
        << L"-------\r\n"
        << L"Esc            : Quit\r\n"
        << L"F1             : Show this help\r\n"
        << L"V              : Toggle VSync\r\n"
        << L"F11 / Alt+Enter: Toggle borderless fullscreen\r\n"
        << L"F10            : Toggle frame pacing stats in title bar\r\n"
        << L"F9             : Toggle RAWINPUT mouse (drag deltas)\r\n"
        << L"F8             : Cycle DXGI max frame latency (1..16)\r\n"
        << L"F7             : Toggle pause-when-unfocused\r\n"
        << L"F6             : Cycle FPS cap when VSync is OFF (∞ / 60 / 120 / 144 / 165 / 240)\r\n"
        << L"Shift+F6        : Cycle background FPS cap (∞ / 5 / 10 / 30 / 60)\r\n"
        << L"\r\n"
        << L"Current\r\n"
        << L"-------\r\n"
        << L"VSync            : " << (m_gfx.VsyncEnabled() ? L"ON" : L"OFF") << L"\r\n"
        << L"Cap (VSync OFF)  : " << CapStr(m_impl ? m_impl->pacer.MaxFpsWhenVsyncOff() : 0u) << L"\r\n"
        << L"Cap (Background) : " << CapStr(m_impl ? m_impl->pacer.MaxFpsWhenUnfocused() : 0u) << L"\r\n"
        << L"Max Frame Latency: " << (m_impl ? std::to_wstring(m_impl->settings.maxFrameLatency) : L"?") << L"\r\n"
        << L"Raw Mouse        : " << (m_impl && m_impl->settings.rawMouse ? L"ON" : L"OFF") << L"\r\n"
        << L"Pause Unfocused  : " << (m_impl && m_impl->settings.pauseWhenUnfocused ? L"ON" : L"OFF") << L"\r\n"
        << L"\r\n"
        << L"Settings persist in %LOCALAPPDATA%\\ColonyGame\\settings.json\r\n";

    const auto msg = oss.str();
    MessageBoxW(m_hwnd ? m_hwnd : nullptr, msg.c_str(), L"Colony Game - Hotkeys", MB_OK | MB_ICONINFORMATION);
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

    const auto CapStr = [](std::uint32_t v) -> std::wstring {
        if (v == 0) return L"∞";
        return std::to_wstring(v);
    };

    std::wostringstream oss;
    oss.setf(std::ios::fixed);
    oss << L"Colony Game | " << std::setprecision(0) << fps << L" FPS"
        << L" | VSync " << vs
        << L" | Lat " << m_impl->settings.maxFrameLatency
        << L" | CapOff " << CapStr(m_impl->pacer.MaxFpsWhenVsyncOff())
        << L" | CapBG " << CapStr(m_impl->pacer.MaxFpsWhenUnfocused())
        << L" | Raw " << (m_impl->settings.rawMouse ? L"ON" : L"OFF")
        << L" | PauseBG " << (m_impl->settings.pauseWhenUnfocused ? L"ON" : L"OFF")
        << L" | " << fs
        << L" | " << act;

    if (!m_impl->settingsLoaded)
    {
        oss << L" | CFG DEFAULT";
    }

    const auto dropped = m_impl->input.Dropped();
    if (dropped > 0)
    {
        oss << L" | InputDrop " << dropped;
    }

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
