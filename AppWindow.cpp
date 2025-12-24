#include "AppWindow_Internal.h"

#include "core/Log.h"

#include <algorithm>
#include <string>

AppWindow::AppWindow() = default;

AppWindow::~AppWindow()
{
#if defined(COLONY_WITH_IMGUI)
    if (m_impl && m_impl->imguiInitialized)
    {
        m_impl->imgui.shutdown();
        m_impl->imguiInitialized = false;
    }
#endif
}

bool AppWindow::Create(HINSTANCE hInst, int nCmdShow, int width, int height)
{
    m_impl = std::make_unique<Impl>();

    // Default window size.
    m_width = (width > 0) ? width : 1280;
    m_height = (height > 0) ? height : 720;

    // Load persisted settings (if any).
    colony::appwin::UserSettings loaded{};
    if (colony::appwin::LoadUserSettings(loaded))
    {
        m_impl->settings = loaded;
        m_impl->settingsLoaded = true;

        // If caller didn't supply an explicit size, use the saved size.
        if (width <= 0 && loaded.windowWidth > 0)
            m_width = loaded.windowWidth;
        if (height <= 0 && loaded.windowHeight > 0)
            m_height = loaded.windowHeight;
    }
    else
    {
        // Ensure we persist something sensible on first run.
        m_impl->settings.windowWidth = m_width;
        m_impl->settings.windowHeight = m_height;
    }

    // Apply settings → runtime.
    m_vsync = m_impl->settings.vsync;
    m_impl->pacer.SetMaxFpsWhenVsyncOff(m_impl->settings.maxFpsWhenVsyncOff);
    m_impl->pacer.SetMaxFpsWhenUnfocused(m_impl->settings.maxFpsWhenUnfocused);

    // Debug overlay preference.
    m_impl->overlayVisible = m_impl->settings.overlayVisible;

    // Fixed-step simulation settings (clamped to sane ranges).
    m_impl->simTickHz = std::clamp(m_impl->settings.simTickHz, 1.0, 1000.0);
    m_impl->simFixedDt = 1.0 / m_impl->simTickHz;
    m_impl->simMaxStepsPerFrame = std::clamp(m_impl->settings.simMaxStepsPerFrame, 1, 240);
    m_impl->simMaxFrameDt = std::clamp(m_impl->settings.simMaxFrameDt, 0.001, 1.0);
    m_impl->simTimeScale = std::clamp(m_impl->settings.simTimeScale, 0.0f, 16.0f);

    // -------------------------------------------------------------------------
    // Register window class.
    // -------------------------------------------------------------------------
    const wchar_t* const CLASS_NAME = L"ColonyGameWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = AppWindow::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    // -------------------------------------------------------------------------
    // Create window.
    // -------------------------------------------------------------------------
    RECT wr{ 0, 0, m_width, m_height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Colony Game Prototype",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        hInst,
        this);

    if (!m_hwnd)
        return false;

    // Initialize helper systems.
    m_impl->mouse.Initialize(m_hwnd);
    m_impl->fullscreen.InitFromCurrent(m_hwnd);

    // Initialize D3D.
    if (!m_gfx.Init(m_hwnd, static_cast<UINT>(m_width), static_cast<UINT>(m_height)))
        return false;

#if defined(COLONY_WITH_IMGUI)
    // Minimal overlay by default (no docking/viewports).
    m_impl->imgui.enabled = true;
    m_impl->imgui.enableDocking = false;
    m_impl->imgui.enableViewports = false;
    m_impl->imgui.drawDockspaceAndMenu = false;
    m_impl->imgui.drawImGuiDebugWindows = false;

    m_impl->imguiInitialized = m_impl->imgui.initialize(m_hwnd, m_gfx.Device(), m_gfx.Context());
    if (!m_impl->imguiInitialized)
        colony::LogLine(L"[ImGui] Failed to initialize");
#endif

    // Show window (respect saved maximize state).
    const int showCmd = m_impl->settings.maximize ? SW_MAXIMIZE : nCmdShow;
    ShowWindow(m_hwnd, showCmd);
    UpdateWindow(m_hwnd);

    // Apply saved fullscreen preference.
    if (m_impl->settings.fullscreen)
    {
        // Apply without marking settings dirty.
        m_impl->fullscreen.Toggle(m_hwnd);

        RECT cr{};
        GetClientRect(m_hwnd, &cr);
        m_width = static_cast<int>(cr.right - cr.left);
        m_height = static_cast<int>(cr.bottom - cr.top);
        m_gfx.Resize(static_cast<UINT>(m_width), static_cast<UINT>(m_height));
    }

    UpdateTitle();
    return true;
}

void AppWindow::MarkSettingsDirty()
{
    if (!m_impl)
        return;

    m_impl->settingsDirty = true;
    m_impl->settingsDirtySince = std::chrono::steady_clock::now();
}

void AppWindow::ToggleVsync()
{
    m_vsync = !m_vsync;

    if (m_impl)
    {
        m_impl->settings.vsync = m_vsync;
        MarkSettingsDirty();
    }

    UpdateTitle();
}

void AppWindow::ToggleFullscreen()
{
    if (!m_impl)
        return;

    m_impl->fullscreen.Toggle(m_hwnd);
    m_impl->settings.fullscreen = m_impl->fullscreen.IsFullscreen();

    // Ensure swapchain matches the new client size.
    RECT cr{};
    GetClientRect(m_hwnd, &cr);
    m_width = static_cast<int>(cr.right - cr.left);
    m_height = static_cast<int>(cr.bottom - cr.top);
    m_gfx.Resize(static_cast<UINT>(m_width), static_cast<UINT>(m_height));

    MarkSettingsDirty();
    UpdateTitle();
}

void AppWindow::ToggleOverlay()
{
    if (!m_impl)
        return;

    m_impl->overlayVisible = !m_impl->overlayVisible;
    m_impl->settings.overlayVisible = m_impl->overlayVisible;
    MarkSettingsDirty();
}

void AppWindow::UpdateTitle()
{
    if (!m_hwnd || !m_impl)
        return;

    const auto cam = m_impl->game.GetDebugCameraInfo();

    const bool isFullscreen = m_impl->fullscreen.IsFullscreen();
    const bool isActive = m_impl->active;

    wchar_t title[512]{};
    swprintf_s(
        title,
        L"Colony Game | %s%s | %s | FPS: %.1f | Sim: %.1f Hz%s | Yaw: %.0f Pitch: %.0f Dist: %.1f",
        m_vsync ? L"VSync" : L"NoVSync",
        isFullscreen ? L" | Fullscreen" : L"",
        isActive ? L"Active" : L"Background",
        m_impl->pacer.Fps(),
        m_impl->simTickHz,
        m_impl->simPaused ? L" (Paused)" : L"",
        cam.yawDeg,
        cam.pitchDeg,
        cam.distance);

    SetWindowTextW(m_hwnd, title);
}
