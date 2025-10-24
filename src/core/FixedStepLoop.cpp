#include "FixedStepLoop.h"
#include "platform/win/HiResClock.h"
#include <windows.h>

void FixedStepLoop::Run(const FixedStepConfig& cfg) {
    fixed_dt_ = cfg.fixed_dt;
    accumulator_ = 0.0;

    HiResClock clock;
    clock.Reset();

    while (IsRunning && IsRunning()) {

        if (PumpOS) PumpOS(); // PeekMessage/Translate/Dispatch in here

        // Measure frame time and clamp nasty spikes (sleep/resume etc.)
        double frame_dt = clock.Tick();
        if (frame_dt > cfg.max_frame_dt) frame_dt = cfg.max_frame_dt;
        if (frame_dt < 0.0) frame_dt = 0.0;

        // Optional: pause sim if minimized to avoid runaway catch-up after restore
        if (IsMinimized && cfg.pause_when_minimized && IsMinimized())
            frame_dt = 0.0;

        accumulator_ += frame_dt;

        // Run as many fixed steps as we need (with a guard to avoid spending
        // entire frames only simulating under heavy load)
        int steps_this_frame = 0;
        while (accumulator_ >= fixed_dt_ && steps_this_frame < cfg.max_steps_per_frame) {
            if (SampleInputsForStep) SampleInputsForStep(step_id_);
            if (UpdateFixed)         UpdateFixed(fixed_dt_);
            accumulator_ -= fixed_dt_;
            ++step_id_;
            ++steps_this_frame;
        }

        // If we still have too much to catch up, drop the extra accumulator time.
        // (Trade-off: keeps the app responsive; if you need strict "real-time"
        // determinism against wall-clock, remove this line.)
        if (accumulator_ > fixed_dt_ * cfg.max_steps_per_frame) {
            accumulator_ = std::fmod(accumulator_, fixed_dt_);
        }

        const double alpha = (fixed_dt_ > 0.0) ? (accumulator_ / fixed_dt_) : 0.0;
        if (Render) Render(std::clamp(alpha, 0.0, 1.0));
    }
}
