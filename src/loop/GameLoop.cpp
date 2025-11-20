#include "colony/loop/GameLoop.h"
#include "colony/world/World.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>
#include <windowsx.h>

namespace colony {

static bool PumpWindowsMessages(HWND hwnd, int& outExitCode) {
    // hwnd is currently unused (we pass nullptr to PeekMessageW),
    // so mark it explicitly to silence C4100.
    (void)hwnd;

    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            outExitCode = static_cast<int>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

int RunGameLoop(World& world, RenderCallback render, HWND hwnd, const GameLoopConfig& cfg) {
    using clock = std::chrono::steady_clock; // monotonic, ideal for intervals
    static_assert(clock::is_steady, "steady_clock must be steady");

    auto previous = clock::now();
    double accumulator = 0.0;

    // Optional: quick sanity content (remove in real game)
    if (world.sim_step() == 0) {
        world.spawn_agent({ 0.0, 0.0 }, { 1.00, 0.50 });
        world.spawn_agent({ 5.0, 2.0 }, { -0.25, 0.75 });
    }

    int  exitCode = 0;
    bool running = true;

    while (running) {
        // 1) OS messages first
        if (!PumpWindowsMessages(hwnd, exitCode)) {
            running = false;
            break;
        }

        // Respect minimize if desired to save CPU
        if (!cfg.run_when_minimized && IsIconic(hwnd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            continue;
        }

        // 2) Measure frame time; clamp extreme values
        auto now = clock::now();
        double frameTime = std::chrono::duration<double>(now - previous).count();
        previous = now;
        frameTime = std::min(frameTime, cfg.max_frame_time);

        accumulator += frameTime;

        // 3) Advance the simulation in fixed steps
        int updates = 0;
        while (accumulator >= cfg.fixed_dt) {
            world.tick(cfg.fixed_dt);
            accumulator -= cfg.fixed_dt;

            if (++updates >= cfg.max_updates_per_frame) {
                // Avoid unbounded catch-up: drop remainder this frame
                accumulator = 0.0;
                break;
            }
        }

        // 4) Interpolation factor for smooth rendering (0..1)
        float alpha = 0.0f;
        if (cfg.fixed_dt > 0.0) {
            alpha = static_cast<float>(accumulator / cfg.fixed_dt);
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
        }

        // 5) Render once per frame
        if (render) {
            render(world, alpha);
        }

        // If you're not using Present() vsync throttling, consider yielding:
        // std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }

    return exitCode;
}

} // namespace colony
