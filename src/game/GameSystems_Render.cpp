#include "GameSystems.hpp"
#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_ZONE(name_literal) ((void)0)
#endif

// Normalize ImGui availability across possible build flags.
// We accept either CG_ENABLE_IMGUI (preferred) or COLONY_ENABLE_IMGUI
// and expose a single internal COLONY_HAVE_IMGUI switch for this TU.
#if defined(CG_ENABLE_IMGUI) || defined(COLONY_ENABLE_IMGUI)
  #include <imgui.h>
  #define COLONY_HAVE_IMGUI 1
#else
  #define COLONY_HAVE_IMGUI 0
#endif

namespace colony {

namespace {
#if COLONY_HAVE_IMGUI
// Simple exponential moving average for FPS to smooth jitter.
struct FpsEma {
  float ema = 0.0f;
  bool primed = false;
  void push(float dt_seconds) {
    if (dt_seconds > 0.0f) {
      const float fps = 1.0f / dt_seconds;
      if (!primed) { ema = fps; primed = true; }
      else         { ema = 0.9f * ema + 0.1f * fps; }
    }
  }
};
#endif // COLONY_HAVE_IMGUI
} // namespace

void RenderFrame([[maybe_unused]] entt::registry& r,
                 [[maybe_unused]] const GameTime& gt) {
  COLONY_TRACY_ZONE("RenderFrame");

  // Your existing renderer should be invoked here (D3D11/D3D12, etc.).
  // This file intentionally avoids coupling to DirectX types.

#if COLONY_HAVE_IMGUI
  // Lightweight HUD when ImGui is available.
  if (ImGui::GetCurrentContext()) {
    static FpsEma s_fps;
    s_fps.push(static_cast<float>(gt.dt_seconds));

    if (ImGui::Begin("Colony HUD")) {
      ImGui::Text("Frame: %llu", static_cast<unsigned long long>(gt.frame_index));
      ImGui::Text("dt (ms): %.3f", gt.dt_seconds * 1000.0);
      ImGui::Text("t  (s): %.3f", gt.time_since_start);
      ImGui::Separator();
      ImGui::Text("FPS (EMA): %.1f", s_fps.ema);
      ImGui::Text("Entities: %zu", static_cast<size_t>(r.alive()));
    }
    ImGui::End();
  } else {
    // Compiled with ImGui but not initialized: warn once to help debugging.
    static bool warned = false;
    if (!warned) {
      spdlog::warn("RenderFrame: ImGui context not available (HUD skipped).");
      warned = true;
    }
  }
#else
  // When ImGui is compiled out, keep parameters intentionally unused.
  (void)r; (void)gt;
#endif
}

} // namespace colony
