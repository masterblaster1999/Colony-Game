#pragma once
#ifndef CG_FIXED_TIMESTEP_H
#define CG_FIXED_TIMESTEP_H

#include <windows.h>
#include <algorithm>
#include <cstdint>

/*
  FixedTimestep (Windows-only)
  - High-resolution timing via QueryPerformanceCounter (QPC).
  - Fixed simulation step with accumulator + interpolation alpha.
  - Spiral-of-death guard: clamp large frame gaps (default 0.25s).
  References:
    * Glenn Fiedler, "Fix Your Timestep!" (accumulator + interpolation).
    * Microsoft Learn, QPC for high-resolution timing.
*/

namespace cg {

class FixedTimestep {
public:
  explicit FixedTimestep(double hz = 60.0) {
    set_hz(hz);
    ::QueryPerformanceFrequency(&freq_);
    ::QueryPerformanceCounter(&last_);
  }

  // Change simulation rate at runtime (e.g., 30 or 60 Hz).
  void set_hz(double hz) {
    target_dt_ = (hz > 0.0) ? (1.0 / hz) : (1.0 / 60.0);
  }
  double target_dt() const { return target_dt_; }

  // Maximum real time (in seconds) we allow to accumulate in one frame.
  // Prevents "spiral of death" after a breakpoint or long stall.
  void set_max_catchup(double seconds) { max_catchup_ = (seconds > 0.0) ? seconds : 0.25; }
  double max_catchup() const { return max_catchup_; }

  // Reset the clock (e.g., after pause/resume or resize)
  void reset() {
    accum_ = 0.0;
    alpha_ = 0.0;
    ::QueryPerformanceCounter(&last_);
  }

  // Tick once per render frame:
  //  - Calls update(dt) 0..N times with fixed dt
  //  - Computes interpolation alpha() for rendering
  //  - max_steps caps CPU use if the sim is very far behind
  template<class UpdateFn>
  void tick(UpdateFn&& update, int max_steps = 8) {
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
    while (accum_ >= target_dt_ && steps < max_steps) {
      // Your sim should snapshot "previous" state before mutating to "current".
      update(target_dt_);
      accum_ -= target_dt_;
      ++steps;
    }
    const double a = accum_ / target_dt_;
    alpha_ = (a < 0.0) ? 0.0 : (a > 1.0 ? 1.0 : a);
  }

  // Fraction [0,1] between the most recent (previous->current) sim states.
  float alpha() const { return static_cast<float>(alpha_); }

private:
  LARGE_INTEGER last_{};
  LARGE_INTEGER freq_{};
  double accum_      {0.0};
  double target_dt_  {1.0 / 60.0};
  double alpha_      {0.0};
  double max_catchup_{0.25}; // seconds
};

} // namespace cg

// -----------------------------------------------------------------------------
// Backward-compatibility alias: allow legacy core::FixedTimestep without
// breaking existing code that includes this header and uses core::FixedTimestep.
// -----------------------------------------------------------------------------
namespace core {
  using FixedTimestep = cg::FixedTimestep;
}

#endif // CG_FIXED_TIMESTEP_H
