#pragma once
#include <functional>
#include <windows.h>

namespace colony {

class World;

// Your renderer provides this callback; it receives alpha (0..1).
using RenderCallback = std::function<void(const World&, float alpha)>;

struct GameLoopConfig {
    double fixed_dt = 1.0 / 60.0;      // simulation step (seconds)
    double max_frame_time = 0.25;      // clamp giant frames to avoid spirals
    int    max_updates_per_frame = 5;  // safety: drop remainder if too far behind
    bool   run_when_minimized = false; // save CPU when minimized
};

// Runs a deterministic fixed-timestep simulation and calls `render` once per frame.
// Returns WM_QUIT exit code.
int RunGameLoop(World& world, RenderCallback render, HWND hwnd, const GameLoopConfig& cfg);

} // namespace colony
