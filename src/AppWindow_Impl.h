#pragma once

// Internal implementation details for AppWindow.
//
// This header is intentionally *not* installed and should only be included by
// AppWindow translation units.

#include "AppWindow.h"

#include "UserSettings.h"
#include "game/PrototypeGame.h"
#include "input/InputEvent.h"
#include "input/InputQueue.h"
#include "loop/FramePacer.h"
#include "loop/FramePacingStats.h"
#include "platform/win32/RawMouseInput.h"
#include "platform/win32/Win32Window.h"

#include <chrono>
#include <cstdint>
#include <limits>

// ----------------------------------------------------------------------------
// AppWindow::Impl
// ----------------------------------------------------------------------------

struct AppWindow::Impl {
    colony::appwin::win32::RawMouseInput         mouse;
    colony::appwin::win32::BorderlessFullscreen  fullscreen;
    colony::input::InputQueue                   input;
    colony::game::PrototypeGame                 game;
    colony::appwin::FramePacer                  pacer;
    colony::appwin::FramePacingStats            frameStats;

    colony::appwin::UserSettings                settings;
    bool                                       settingsLoaded = false;
    bool                                       settingsDirty = false;

    // Debounced auto-save for settings writes.
    //
    // We avoid writing settings.json on every WM_SIZE during interactive resizing,
    // but we also don't want to lose changes if the app crashes.
    std::chrono::steady_clock::time_point       nextSettingsAutoSave{};
    bool                                       hasPendingAutoSave = false;

    // Window state
    bool                                       active = true;

    // When resizing via the window frame, defer swapchain resizes until the
    // user finishes the drag (WM_EXITSIZEMOVE). This avoids hammering
    // ResizeBuffers on every mouse move during sizing.
    bool                                       inSizeMove = false;
    UINT                                       pendingResizeW = 0;
    UINT                                       pendingResizeH = 0;

    // Mouse delta aggregation (prevents InputQueue overflow with very high
    // polling rate mice; flushed into a single MouseDelta event per pump).
    long long                                  pendingMouseDx = 0;
    long long                                  pendingMouseDy = 0;

    // How long to wait after the *last* settings change before writing settings.json.
    static constexpr auto kSettingsAutoSaveDelay = std::chrono::milliseconds(750);
    // If a write fails (e.g., transient AV scan/lock), back off before retrying.
    static constexpr auto kSettingsAutoSaveRetryDelay = std::chrono::seconds(2);

    void ScheduleSettingsAutosave() noexcept
    {
        settingsDirty = true;
        hasPendingAutoSave = true;
        nextSettingsAutoSave = std::chrono::steady_clock::now() + kSettingsAutoSaveDelay;
    }

    void MaybeAutoSaveSettings() noexcept
    {
        if (!settingsDirty || !hasPendingAutoSave)
            return;

        // Don't write mid-drag; wait for WM_EXITSIZEMOVE.
        if (inSizeMove)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now < nextSettingsAutoSave)
            return;

        if (colony::appwin::SaveUserSettings(settings))
        {
            settingsLoaded = true;
            settingsDirty = false;
            hasPendingAutoSave = false;
            return;
        }

        // Retry later.
        nextSettingsAutoSave = now + kSettingsAutoSaveRetryDelay;
        hasPendingAutoSave = true;
    }

    [[nodiscard]] DWORD BackgroundWaitTimeoutMs() const noexcept
    {
        if (!settingsDirty || !hasPendingAutoSave || inSizeMove)
            return INFINITE;

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextSettingsAutoSave)
            return 0;

        // Clamp timeout to avoid overflow.
        const auto remaining = nextSettingsAutoSave - now;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        if (ms <= 0)
            return 0;
        if (ms > 60'000)
            return 60'000;
        return static_cast<DWORD>(ms);
    }

    void FlushPendingMouseDelta() noexcept
    {
        if (pendingMouseDx == 0 && pendingMouseDy == 0)
            return;

        const auto b = mouse.Buttons();

        colony::input::InputEvent ev{};
        ev.type = colony::input::InputEventType::MouseDelta;
        ev.dx = ClampI32(pendingMouseDx);
        ev.dy = ClampI32(pendingMouseDy);
        ev.buttons = 0;
        if (b.left)   ev.buttons |= colony::input::MouseButtonsMask::MouseLeft;
        if (b.right)  ev.buttons |= colony::input::MouseButtonsMask::MouseRight;
        if (b.middle) ev.buttons |= colony::input::MouseButtonsMask::MouseMiddle;
        if (b.x1)     ev.buttons |= colony::input::MouseButtonsMask::MouseX1;
        if (b.x2)     ev.buttons |= colony::input::MouseButtonsMask::MouseX2;

        input.Push(ev);
        pendingMouseDx = 0;
        pendingMouseDy = 0;
    }

private:
    static std::int32_t ClampI32(long long v) noexcept
    {
        if (v < static_cast<long long>(std::numeric_limits<std::int32_t>::min()))
            return std::numeric_limits<std::int32_t>::min();
        if (v > static_cast<long long>(std::numeric_limits<std::int32_t>::max()))
            return std::numeric_limits<std::int32_t>::max();
        return static_cast<std::int32_t>(v);
    }
};
