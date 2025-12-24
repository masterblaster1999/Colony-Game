#pragma once

#include <cstdint>
#include <filesystem>

namespace colony::appwin {

// Lightweight persisted settings for the current user.
//
// Stored in:
//   %LOCALAPPDATA%\ColonyGame\settings.json
//
// (Windows-only project; LocalAppData keeps configs per-user and avoids UAC.)
struct UserSettings
{
    // Windowed-mode client area size (ignored when fullscreen is enabled).
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;

    // Present with vsync enabled.
    bool vsync = true;

    // Borderless fullscreen.
    bool fullscreen = false;

    // Safety cap to avoid pegging a CPU core when vsync is off.
    // 0 = uncapped (not recommended for laptops).
    int maxFpsWhenVsyncOff = 240;

    // If true, the game stops rendering/sim ticking when the window is not the
    // foreground app (Alt+Tab). This saves a lot of CPU/GPU and avoids
    // surprising background input.
    bool pauseWhenUnfocused = true;

    // If pauseWhenUnfocused is false, this is an optional FPS cap used while the
    // window is unfocused. 0 = uncapped.
    int maxFpsWhenUnfocused = 30;

    // Debug overlay (ImGui). Toggled with F1.
    bool overlayVisible = true;

    // Fixed-step simulation loop settings.
    //
    // tickHz controls the fixed dt = 1/tickHz used for simulation updates.
    // maxStepsPerFrame prevents spiral-of-death catch-up.
    // maxFrameDt clamps large time gaps (alt-tab/minimize) before accumulation.
    double simTickHz = 60.0;
    int    simMaxStepsPerFrame = 8;
    double simMaxFrameDt = 0.25;
    float  simTimeScale = 1.0f;
};

[[nodiscard]] std::filesystem::path UserSettingsPath();

// Returns true if a settings file existed and was successfully parsed.
// On failure, `out` is left unchanged (callers should initialize defaults first).
[[nodiscard]] bool LoadUserSettings(UserSettings& out) noexcept;

// Returns true on success.
[[nodiscard]] bool SaveUserSettings(const UserSettings& settings) noexcept;

} // namespace colony::appwin
