// src/core/FixedTimestep.h
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>

#include "core/Profile.h"

namespace colony {

struct FixedSettings {
  double tick_hz = 60.0;        // simulation Hz
  int    max_catchup_ticks = 5; // clamp catch-up per frame
  double max_frame_dt = 0.25;   // seconds; clamp big pauses
};

struct StepStats {
  int    ticks_this_frame = 0;  // how many sim ticks we ran
  double fixed_dt        = 0.0; // the sim DT (1/tick_hz)
  double frame_dt        = 0.0; // unclamped frame delta
  double clamped_dt      = 0.0; // clamped delta we actually used
  double alpha           = 0.0; // interpolation factor [0,1]
};

class FixedStepper {
public:
  explicit FixedStepper(const FixedSettings& s)
  : settings_{s}, fixed_dt_(1.0 / std::max(1.0, s.tick_hz)) {}

  void reset(double now_seconds) {
    last_time_ = now_seconds;
    accumulator_ = 0.0;
  }

  // update:   void(double dt_seconds)
  // render:   void(float alpha)
  StepStats step(double now_seconds,
                 const std::function<void(double)>& update,
                 const std::function<void(float)>&  render)
  {
    CG_ZONE("FixedStepper::step");

    StepStats stats{};
    stats.fixed_dt = fixed_dt_;

    double raw_dt = now_seconds - last_time_;
    // Guard against clock going backwards slightly.
    raw_dt = std::max(0.0, raw_dt);
    stats.frame_dt = raw_dt;

    // Clamp long pauses (alt‑tab, breakpoint, etc.)
    const double clamped = std::min(raw_dt, settings_.max_frame_dt);
    stats.clamped_dt = clamped;

    last_time_ = now_seconds;
    accumulator_ += clamped;

    // Catch-up loop
    int ticks = 0;
    while (accumulator_ >= fixed_dt_ && ticks < settings_.max_catchup_ticks) {
      CG_ZONE("SimTick");
      update(fixed_dt_);
      accumulator_ -= fixed_dt_;
      ++ticks;
    }
    stats.ticks_this_frame = ticks;

    // Interpolate (0..1)
    const double denom = (fixed_dt_ <= 0.0 ? 1.0 : fixed_dt_);
    double alpha = accumulator_ / denom;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    stats.alpha = alpha;

    // Render at display rate using interpolation factor.
    {
      CG_ZONE("Render");
      render(static_cast<float>(alpha));
    }

    CG_FRAME_MARK();
    return stats;
  }

  double fixed_dt() const { return fixed_dt_; }

private:
  FixedSettings settings_{};
  double fixed_dt_ = 1.0 / 60.0;
  double accumulator_ = 0.0;
  double last_time_ = 0.0;
};

} // namespace colony
