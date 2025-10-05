#pragma once
#ifndef CG_FIXED_TIMESTEP_H
#define CG_FIXED_TIMESTEP_H

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <functional>

/*
  FixedTimestep (Windows-only) — launcher-aligned
  - High-resolution timing via QueryPerformanceCounter (QPC).
  - Fixed simulation step with accumulator + interpolation alpha.
  - Spiral-of-death guard: clamp large frame gaps (default 0.25s).
  - Matches launcher expectations:
      * namespace: core
      * set_max_steps_per_frame(int)
      * tick(update, render) -> FixedTimestepStats
      * exposes per-frame stats (fps, steps, total, alpha)
*/

namespace core {

struct FixedTimestepStats {
  double fps             = 0.0;  // smoothed frames per second
  int    steps_this_frame = 0;   // how many fixed updates ran this frame
  int    total_steps      = 0;   // total fixed updates since start/reset
  double alpha            = 0.0; // interpolation factor [0,1] for rendering
};

class FixedTimestep {
public:
  explicit FixedTimestep(double hz = 60.0) {
    set_hz(hz);
    ::QueryPerformanceFrequency(&freq_);
    ::QueryPerformanceCounter(&last_);
  }

  // Change simulation rate at runtime (e.g., 30 or 60 Hz).
  void   set_hz(double hz)          { target_dt_ = (hz > 0.0) ? (1.0 / hz) : (1.0 / 60.0); }
  double target_dt() const          { return target_dt_; }

  // Cap how much real time (in seconds) we allow to accumulate in one frame.
  // Prevents "spiral of death" after a breakpoint or long stall.
  void   set_max_catchup(double s)  { max_catchup_ = (s > 0.0) ? s : 0.25; }
  double max_catchup() const        { return max_catchup_; }

  // Limit how many fixed steps we perform per render frame.
  void   set_max_steps_per_frame(int steps) { max_steps_per_frame_ = (steps > 0) ? steps : 8; }
  int    max_steps_per_frame() const        { return max_steps_per_frame_; }

  // Reset the clock (e.g., after pause/resume or resize)
  void reset() {
    accum_ = 0.0;
    alpha_ = 0.0;
    frame_time_ema_ = target_dt_;
    total_steps_ = 0;
    ::QueryPerformanceCounter(&last_);
  }

  // Tick once per render frame:
  //  - Calls update(dt) 0..N times with fixed dt
  //  - Computes interpolation alpha in [0,1] and calls render(alpha)
  //  - Enforces max_steps_per_frame_ to cap CPU in heavy catch-up scenarios
  template<typename UpdateFn, typename RenderFn>
  FixedTimestepStats tick(UpdateFn&& update, RenderFn&& render) {
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);

    const double seconds =
      static_cast<double>(now.QuadPart - last_.QuadPart) /
      static_cast<double>(freq_.QuadPart);
    last_ = now;

    // Clamp to avoid unbounded catch-up (breakpoints, window drag, etc.)
    const double frame = (seconds > max_catchup_) ? max_catchup_ : seconds;
    accum_ += frame;

    int steps = 0;
    while (accum_ >= target_dt_ && steps < max_steps_per_frame_) {
      update(target_dt_);
      accum_ -= target_dt_;
      ++steps;
      ++total_steps_;
    }

    // Interpolation factor between previous and current sim states.
    const double a = accum_ / target_dt_;
    alpha_ = std::clamp(a, 0.0, 1.0);

    // Render with interpolation alpha
    render(alpha_);

    // Cheap smoothed FPS using EMA of frame times.
    // (EMA chosen over instantaneous 1/frame for stability.)
    frame_time_ema_ = 0.9 * frame_time_ema_ + 0.1 * frame;

    FixedTimestepStats stats;
    stats.fps = (frame_time_ema_ > 0.0) ? (1.0 / frame_time_ema_) : 0.0;
    stats.steps_this_frame = steps;
    stats.total_steps = total_steps_;
    stats.alpha = alpha_;
    return stats;
  }

  // Optional getter for code that still queries alpha directly.
  float alpha() const { return static_cast<float>(alpha_); }

private:
  LARGE_INTEGER last_{};
  LARGE_INTEGER freq_{};

  double accum_              {0.0};
  double target_dt_          {1.0 / 60.0};
  double alpha_              {0.0};
  double max_catchup_        {0.25}; // seconds
  int    max_steps_per_frame_{8};
  int    total_steps_        {0};

  // Exponential moving average of frame time (seconds)
  double frame_time_ema_     {1.0 / 60.0};
};

} // namespace core

// -----------------------------------------------------------------------------
// Backward-compatibility aliases: allow legacy cg::FixedTimestep usage
// without breaking existing code that included this header in the past.
// -----------------------------------------------------------------------------
namespace cg {
  using FixedTimestep      = core::FixedTimestep;
  using FixedTimestepStats = core::FixedTimestepStats;
}

#endif // CG_FIXED_TIMESTEP_H
