// include/core/Application.hpp
#pragma once

#if !defined(_WIN32)
#error "Colony-Game targets Windows only."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace core
{
struct ApplicationDesc
{
    const wchar_t* title = L"Colony Game";
    int width = 1280;
    int height = 720;

    // If your presentation path blocks on vsync, set this true and the loop
    // won’t need to yield. If you run uncapped, set false so we yield.
    bool vsync = true;

    // Fixed simulation step (seconds).
    double fixed_dt_seconds = 1.0 / 60.0;

    // Clamp long frames to avoid spiral-of-death when debugging/breakpointing.
    double max_frame_time_seconds = 0.25;
};

// Entry used by WinLauncher / platform entry.
// Returns process exit code.
int RunApplication(HINSTANCE hInstance, int nCmdShow, ApplicationDesc desc = {});
} // namespace core
