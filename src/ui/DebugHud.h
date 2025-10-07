// src/ui/DebugHud.h
#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "core/Profile.h"

#if __has_include(<imgui.h>)
  #include <imgui.h>
  #define CG_WITH_IMGUI 1
#else
  #define CG_WITH_IMGUI 0
#endif

namespace colony {

struct DebugHudMetrics {
  double sim_time_seconds = 0.0;
  double tick_hz          = 60.0;
  int    ticks_this_frame = 0;
  double frame_dt_seconds = 0.0; // unclamped
  double clamped_dt_seconds = 0.0;
  double alpha            = 0.0;
};

class DebugHud {
public:
  explicit DebugHud(size_t history_len = 240)
  : history_len_(static_cast<int>(history_len)) {
    frame_ms_history_.fill(0.0f);
  }

  void set_visible(bool v) { visible_ = v; }
  bool visible() const { return visible_; }

  void update(const DebugHudMetrics& m) {
    // Exponential smoothing for FPS
    const float ms = static_cast<float>(m.clamped_dt_seconds * 1000.0);
    frame_ms_history_[cursor_] = ms;
    cursor_ = (cursor_ + 1) % history_len_;

    const double inst_fps = (m.clamped_dt_seconds > 0.0)
      ? (1.0 / m.clamped_dt_seconds) : 0.0;
    fps_smooth_ = (fps_smooth_ <= 0.01)
      ? inst_fps
      : (fps_smooth_ * 0.90 + inst_fps * 0.10);

    sim_time_ = m.sim_time_seconds;
    tick_hz_  = m.tick_hz;
    ticks_this_frame_ = m.ticks_this_frame;
    alpha_ = m.alpha;

    CG_PLOT("FPS", fps_smooth_);
  }

  void draw() {
#if CG_WITH_IMGUI
    if (!visible_) return;
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Debug HUD", &visible_, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("FPS:  %.1f", fps_smooth_);
      ImGui::Text("Sim:  %.2fs   Tick: %.2f Hz   Ticks/frame: %d   alpha=%.2f",
                  sim_time_, tick_hz_, ticks_this_frame_, alpha_);
      ImGui::Separator();
      ImGui::Text("Frame pacing (ms, last %d):", history_len_);
      // Build a contiguous view from our ring buffer
      std::array<float, 512> tmp{};
      const int n = history_len_;
      for (int i = 0; i < n; ++i) {
        int idx = (cursor_ + i) % n;
        tmp[i] = frame_ms_history_[idx];
      }
      ImGui::PlotHistogram("##frame_ms_hist", tmp.data(), n, 0, nullptr, 0.0f, 50.0f, ImVec2(280, 80));
      ImGui::Spacing();
      ImGui::TextDisabled("Toggle with F1 (example)"); // hook your input path
    }
    ImGui::End();
#endif
  }

private:
  bool visible_ = true;
  int  history_len_ = 0;
  int  cursor_ = 0;
  std::array<float, 512> frame_ms_history_{};
  double fps_smooth_ = 0.0;
  double sim_time_ = 0.0;
  double tick_hz_ = 60.0;
  int    ticks_this_frame_ = 0;
  double alpha_ = 0.0;
};

} // namespace colony
