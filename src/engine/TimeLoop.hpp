// src/engine/TimeLoop.hpp
#pragma once
#include <chrono>
#include <thread>
#include <functional>

namespace engine {

struct LoopConfig {
  double fixed_hz = 60.0;         // simulation rate
  double max_frame_time = 0.25;   // clamp huge spikes (e.g., alt-tab)
  int    max_updates_per_frame = 5;
  bool   sleep_between_frames = true;
  int    sleep_ms = 1;            // 1 ms to avoid busy-wait
};

template <class UpdateFn, class RenderFn, class QuitFn>
void run_loop(UpdateFn&& update_step,
              RenderFn&& render_frame,
              QuitFn&&  should_quit,
              LoopConfig cfg = {}) {
  using clock = std::chrono::steady_clock;
  const double dt = 1.0 / cfg.fixed_hz;
  auto prev = clock::now();
  double accumulator = 0.0;

  while (!should_quit()) {
    const auto now = clock::now();
    double frame_time = std::chrono::duration<double>(now - prev).count();
    prev = now;

    if (frame_time > cfg.max_frame_time) frame_time = cfg.max_frame_time;
    accumulator += frame_time;

    int updates = 0;
    while (accumulator >= dt && updates < cfg.max_updates_per_frame) {
      update_step(dt);
      accumulator -= dt;
      ++updates;
    }
    if (updates == cfg.max_updates_per_frame) {
      // Drop any excess to prevent 'spiral of death'
      accumulator = 0.0;
    }

    const double alpha = accumulator / dt; // 0..1 between last and next sim state
    render_frame(alpha);

    if (cfg.sleep_between_frames) {
      std::this_thread::sleep_for(std::chrono::milliseconds(cfg.sleep_ms));
    }
  }
}

} // namespace engine
