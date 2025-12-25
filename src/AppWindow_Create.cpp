#include "AppWindow_Impl.h"

#include "DxDevice.h"

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
