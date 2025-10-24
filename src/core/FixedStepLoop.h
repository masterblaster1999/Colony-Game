#pragma once
#include <cstdint>
#include <algorithm>
#include <functional>

struct SimState {
    // Position/orientation/etc. Only includes data needed for interpolation.
    // Keep a "render state" snapshot small: it's copied each step for alpha-blend.
};

struct FixedStepConfig {
    double fixed_dt = 1.0 / 60.0;   // simulation step (seconds)
    double max_frame_dt = 0.25;     // clamp large wall-clock deltas (seconds)
    int    max_steps_per_frame = 8; // back-pressure guard
    bool   pause_when_minimized = true;
};

class FixedStepLoop {
public:
    // Callbacks
    std::function<void()>               PumpOS;             // poll window/messages
    std::function<void(double)>         UpdateFixed;        // simulate one tick of length fixed_dt
    std::function<void(double)>         Render;             // render with alpha in [0,1]
    std::function<bool()>               IsRunning;          // check quit flag
    std::function<bool()>               IsMinimized;        // optional

    // Optional: record/consume inputs per-step for determinism
    std::function<void(std::uint64_t)>  SampleInputsForStep;// called before each fixed step

    void Run(const FixedStepConfig& cfg);

    // Accessors to support replays/seeded RNG
    std::uint64_t StepId() const { return step_id_; }
    double FixedDt() const { return fixed_dt_; }

private:
    double accumulator_ = 0.0;
    double fixed_dt_ = 1.0 / 60.0;
    std::uint64_t step_id_ = 0;
};
