#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace colony::appwin {

// -----------------------------------------------------------------------------
// Window sizing guardrails
// -----------------------------------------------------------------------------
// These are *client-area* dimensions (not including the Win32 non-client frame).
// They are used both when clamping persisted settings.json values and when
// enforcing minimum resize constraints via WM_GETMINMAXINFO.
inline constexpr std::uint32_t kMinWindowClientWidth  = 640;
inline constexpr std::uint32_t kMinWindowClientHeight = 360;

// 8K desktop-ish upper bounds (guard against accidental huge values).
inline constexpr std::uint32_t kMaxWindowClientWidth  = 7680;
inline constexpr std::uint32_t kMaxWindowClientHeight = 4320;

// How DXGI should scale the swapchain to the window.
//
// For modern borderless fullscreen + "independent flip" friendliness, `None`
// is typically the best choice when you resize buffers to match the client size.
//
// This is intentionally stored as a project-local enum so UserSettings.h doesn't
// have to include dxgi headers.
enum class SwapchainScalingMode : std::uint8_t {
    None = 0,
    Stretch = 1,
    Aspect = 2,
};

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

    // DXGI maximum frame latency (how many frames the app can queue ahead).
    //
    //  - 1 is the lowest latency and pairs well with a waitable swapchain.
    //  - Larger values can increase throughput but raise latency.
    //
    // NOTE: This is applied via IDXGISwapChain2::SetMaximumFrameLatency when
    // available, with a best-effort fallback to IDXGIDevice1.
    int maxFrameLatency = 1;

    // Swapchain scaling policy.
    SwapchainScalingMode swapchainScaling = SwapchainScalingMode::None;

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

    // --------------------------------------------------------------------------------------------
    // Input / debug
    // --------------------------------------------------------------------------------------------

    // Enable high-resolution RAWINPUT mouse deltas (better high-DPI + high polling stability).
    bool rawMouse = true;

    // Show in-app frame pacing stats in the window title (PresentMon-style summary).
    bool showFrameStats = false;
};

[[nodiscard]] std::filesystem::path UserSettingsPath();

// Returns true if a settings file existed and was successfully parsed.
// On failure, `out` is left unchanged (callers should initialize defaults first).
[[nodiscard]] bool LoadUserSettings(UserSettings& out) noexcept;

// Returns true on success.
[[nodiscard]] bool SaveUserSettings(const UserSettings& settings) noexcept;

} // namespace colony::appwin
