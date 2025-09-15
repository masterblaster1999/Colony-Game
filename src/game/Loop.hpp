// src/game/Loop.hpp
#pragma once
#include <chrono>
#include <thread>
#include "game/GamePublic.hpp" // uses fixedTimeStepHz, maxCatchUpFrames, framePacing, etc.

namespace cg {

struct LoopConfig {
    double fixedHz;           // <= 0 => variable timestep
    int    maxCatchUpFrames;  // cap to prevent spiral-of-death
    int    targetFps;         // <= 0 => uncapped
    FramePacingMode pacing;   // Sleep | BusyWait | Hybrid
    bool   pauseOnFocusLoss;
};

class GameLoop {
public:
    explicit GameLoop(const GameOptions& opt)
        : cfg{ double(opt.fixedTimeStepHz),
               opt.maxCatchUpFrames,
               int(opt.targetFrameRate),
               opt.framePacing,
               opt.pauseOnFocusLoss } {}

    // pump(): poll OS/window/input events
    // update(dtSeconds): advance simulation by dtSeconds (fixed or variable)
    // render(alpha): render (alpha is 0..1 interpolation amount when fixedHz>0)
    // shouldQuit(): whether user requested exit
    // isFocused(): whether our game window has focus (pass []{return true;} if not available)
    template<typename PumpFn, typename UpdateFn, typename RenderFn, typename QuitFn, typename FocusFn>
    int run(PumpFn&& pump, UpdateFn&& update, RenderFn&& render, QuitFn&& shouldQuit, FocusFn&& isFocused) {
        using clock = std::chrono::steady_clock;

        const bool fixed   = (cfg.fixedHz > 0.0);
        const auto step    = fixed ? std::chrono::duration<double>(1.0 / cfg.fixedHz)
                                   : std::chrono::duration<double>(0.0);
        const bool paced   = (cfg.targetFps > 0);
        const auto target  = paced ? std::chrono::duration<double>(1.0 / cfg.targetFps)
                                   : std::chrono::duration<double>(0.0);

        auto prev = clock::now();
        std::chrono::duration<double> acc{0};
        const int maxCatchUp = (cfg.maxCatchUpFrames > 0 ? cfg.maxCatchUpFrames : 5);

        while (!shouldQuit()) {
            auto frameStart = clock::now();
            pump(); // OS/input

            if (cfg.pauseOnFocusLoss && !isFocused()) {
                render(0.0); // keep drawing pause/menu
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                prev = clock::now(); // drop accumulated time while paused
                continue;
            }

            auto now   = clock::now();
            auto delta = now - prev;
            prev       = now;

            if (fixed) {
                acc += delta;
                int steps = 0;
                while (acc >= step && steps < maxCatchUp) {
                    update(step.count());
                    acc -= step;
                    ++steps;
                }
                const double alpha = step.count() > 0 ? acc.count() / step.count() : 0.0;
                render(alpha);
            } else {
                update(std::chrono::duration<double>(delta).count());
                render(0.0);
            }

            if (paced) {
                auto elapsed = clock::now() - frameStart;
                auto left    = target - elapsed;
                if (left > std::chrono::duration<double>(0)) {
                    switch (cfg.pacing) {
                        case FramePacingMode::Sleep:
                            std::this_thread::sleep_for(left);
                            break;
                        case FramePacingMode::Hybrid: {
                            auto sleepPart = left - std::chrono::milliseconds(1);
                            if (sleepPart > std::chrono::duration<double>(0))
                                std::this_thread::sleep_for(sleepPart);
                            while (clock::now() - frameStart < target) { /* busy-wait */ }
                            break;
                        }
                        case FramePacingMode::BusyWait:
                            while (clock::now() - frameStart < target) { /* busy-wait */ }
                            break;
                        default: break;
                    }
                }
            }
        }
        return 0;
    }

private:
    LoopConfig cfg;
};

} // namespace cg
