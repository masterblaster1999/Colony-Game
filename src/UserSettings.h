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

    // DXGI max frame latency (how many frames can be queued for the swapchain).
    //
    //  - 1 is the lowest-latency setting (recommended).
    //  - Higher values can improve throughput on some GPUs but increase input latency.
    //
    // Note: This only applies when the swapchain is created with
    // DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (our default on supported systems).
    int maxFrameLatency = 1;

    // If true, the game stops rendering/sim ticking when the window is not the
    // foreground app (Alt+Tab). This saves a lot of CPU/GPU and avoids
    // surprising background input.
    bool pauseWhenUnfocused = true;

    // If pauseWhenUnfocused is false, this is an optional FPS cap used while the
    // window is unfocused. 0 = uncapped.
    int maxFpsWhenUnfocused = 30;
};

[[nodiscard]] std::filesystem::path UserSettingsPath();

// Returns true if a settings file existed and was successfully parsed.
// On failure, `out` is left unchanged (callers should initialize defaults first).
[[nodiscard]] bool LoadUserSettings(UserSettings& out) noexcept;

// Returns true on success.
[[nodiscard]] bool SaveUserSettings(const UserSettings& settings) noexcept;

} // namespace colony::appwin
