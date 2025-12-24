#pragma once

// Internal implementation details for AppWindow split across multiple translation units.
// This header is intentionally *private* to the executable target.

#include "AppWindow.h"

#include "game/PrototypeGame.h"
#include "input/InputQueue.h"
#include "UserSettings.h"

#include "loop/FramePacer.h"

#include "platform/win/HiResClock.h"
#include "platform/win32/RawMouseInput.h"
#include "platform/win32/Win32Window.h"

#if defined(COLONY_WITH_IMGUI)
    #include "ui/ImGuiLayer.h"
#endif

#include <chrono>

// ----------------------------------------------------------------------------
// AppWindow::Impl
// ----------------------------------------------------------------------------

struct AppWindow::Impl {
    // Input + window helpers
    colony::appwin::win32::RawMouseInput        mouse;
    colony::appwin::win32::BorderlessFullscreen fullscreen;
    colony::input::InputQueue                  input;

    // "Game" prototype layer
    colony::game::PrototypeGame                game;

    // Frame pacing
    colony::appwin::FramePacer                 pacer;

    // Persisted user settings
    colony::appwin::UserSettings               settings;
    bool                                      settingsLoaded = false;
    bool                                      settingsDirty = false;
    std::chrono::steady_clock::time_point      settingsDirtySince{};

    // Window state
    bool                                      active = true;

    // When resizing via the window frame, defer swapchain resizes until the
    // user finishes the drag (WM_EXITSIZEMOVE). This avoids hammering
    // ResizeBuffers on every mouse move during sizing.
    bool                                      inSizeMove = false;
    UINT                                      pendingResizeW = 0;
    UINT                                      pendingResizeH = 0;

    // ---------------------------------------------------------------------
    // Fixed-step simulation (engine-y core loop)
    // ---------------------------------------------------------------------
    double simTickHz = 60.0;          // simulation ticks per second
    int    simMaxStepsPerFrame = 8;   // catchup cap
    double simMaxFrameDt = 0.25;      // clamp huge frame spikes (seconds)

    double simFixedDt = 1.0 / 60.0;   // derived from simTickHz
    double simAccumulator = 0.0;
    double simTimeSeconds = 0.0;

    bool   simPaused = false;
    int    simStepRequests = 0;       // when paused, step N fixed ticks
    float  simTimeScale = 1.0f;       // optional: scales dt passed to sim update

    colony::platform::win::HiResClock simClock;
    bool   simClockInitialized = false;

    // Last-frame stats (for overlay)
    int    simTicksLastFrame = 0;
    double simFrameDt = 0.0;
    double simClampedDt = 0.0;
    double simAlpha = 0.0;
    bool   simDroppedTimeThisFrame = false;
    double simDroppedSecondsThisFrame = 0.0;

#if defined(COLONY_WITH_IMGUI)
    cg::ui::ImGuiLayer imgui;
    bool imguiInitialized = false;

    // F1 toggles this.
    bool overlayVisible = true;
    bool showImGuiDemo = false;
#endif
};
