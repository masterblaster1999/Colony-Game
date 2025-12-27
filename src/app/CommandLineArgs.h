#pragma once

#include <optional>
#include <string>
#include <vector>

namespace colony::appwin {

// Parsed command-line arguments for the ColonyGame executable.
//
// This is intentionally small and Windows-only. It exists to:
//   - Provide a "safe mode" for recovery from bad settings/layout.
//   - Provide dev/test overrides (vsync/fullscreen/latency caps).
//
// Notes:
//   - All option names are case-insensitive.
//   - Both "--flag=value" and "--flag value" forms are supported.
struct CommandLineArgs
{
    bool showHelp = false;          // --help / -h / /?
    bool safeMode = false;          // --safe-mode
    bool resetSettings = false;     // --reset-settings
    bool resetImGui = false;        // --reset-imgui
    bool resetBindings = false;    // --reset-bindings

    bool ignoreSettings = false;    // --ignore-settings (don't read settings.json)
    bool ignoreImGuiIni = false;    // --ignore-imgui-ini (don't read imgui.ini)
    bool disableImGui = false;      // --no-imgui / --no-ui

    std::optional<int> width;       // --width <px>
    std::optional<int> height;      // --height <px>

    std::optional<bool> fullscreen; // --fullscreen / --windowed
    std::optional<bool> vsync;      // --vsync / --novsync
    std::optional<bool> rawMouse;   // --rawmouse / --norawmouse

    std::optional<int> maxFrameLatency;      // --max-frame-latency <1..16>
    std::optional<int> maxFpsWhenVsyncOff;   // --maxfps <0|N>
    std::optional<bool> pauseWhenUnfocused; // --pause-when-unfocused / --no-pause-when-unfocused
    std::optional<int> maxFpsWhenUnfocused;  // --bgfps <0|N>

    // Any unknown/unsupported args are collected here (so we can show a useful error).
    std::vector<std::wstring> unknown;
};

// Parse the process command line using CommandLineToArgvW(GetCommandLineW()).
[[nodiscard]] CommandLineArgs ParseCommandLineArgs();

// Human-readable help text for MessageBoxW / logs.
[[nodiscard]] std::wstring BuildCommandLineHelpText();

} // namespace colony::appwin
