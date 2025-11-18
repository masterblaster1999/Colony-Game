#include "GameSystems.hpp"
#include <spdlog/spdlog.h>
#include <cstddef>          // std::size_t
#include <entt/entt.hpp>    // entt::registry::{alive,each}

// ----- Tracy zones (optional) -------------------------------------------------
#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_ZONE(name_literal) ((void)0)
#endif

// ----- ImGui presence normalization ------------------------------------------
#if defined(CG_ENABLE_IMGUI) || defined(COLONY_ENABLE_IMGUI)
  #include <imgui.h>
  #define COLONY_HAVE_IMGUI 1
#else
  #define COLONY_HAVE_IMGUI 0
#endif

namespace colony {

namespace {

// Robust, version-stable alive-entity count for EnTT.
//
// Strategy:
//  1) If the installed registry type exposes alive(), use it (O(1)).
//  2) Otherwise, fall back to an each() walk and count entities (O(N)).
template <class R>
static std::size_t TryAliveCount(const R& reg, long /*unused*/)
{
  if constexpr (requires(const R& r) { r.alive(); })
  {
    // EnTT basic_registry::alive(): number of entities still in use.
    return static_cast<std::size_t>(reg.alive());
  }
  else if constexpr (requires(const R& r) { r.each([](auto){}); })
  {
    std::size_t alive = 0;
    reg.each([&alive](auto /*entity*/) {
      ++alive;
    });
    return alive;
  }
  else
  {
    // Unknown registry-like type with no alive()/each(); best-effort.
    return 0u;
  }
}

inline std::size_t CountAliveEntities(const entt::registry& reg) {
  // Prefer reg.alive() if it exists on this EnTT version; otherwise use the
  // each()-based fallback above.
  return TryAliveCount(reg, 0L);
}

#if COLONY_HAVE_IMGUI
// Simple exponential moving average for FPS to reduce jitter in the HUD.
struct FpsEma {
  float ema    = 0.0f;
  bool  primed = false;
  void push(float dt_seconds) {
    if (dt_seconds > 0.0f) {
      const float fps = 1.0f / dt_seconds;
      if (!primed) { ema = fps; primed = true; }
      else         { ema = 0.9f * ema + 0.1f * fps; }
    }
  }
};
#endif
} // namespace

void RenderFrame([[maybe_unused]] entt::registry& r,
                 [[maybe_unused]] const GameTime& gt) {
  COLONY_TRACY_ZONE("RenderFrame");

  // This TU deliberately avoids coupling to the renderer's D3D types.

#if COLONY_HAVE_IMGUI
  if (ImGui::GetCurrentContext()) {
    static FpsEma s_fps;
    s_fps.push(static_cast<float>(gt.dt_seconds));

    if (ImGui::Begin("Colony HUD")) {
      ImGui::Text("Frame: %llu", static_cast<unsigned long long>(gt.frame_index));
      ImGui::Text("dt (ms): %.3f", gt.dt_seconds * 1000.0);
      ImGui::Text("t  (s): %.3f", gt.time_since_start);
      ImGui::Separator();
      ImGui::Text("FPS (EMA): %.1f", s_fps.ema);

      // Version-stable alive count (no registry.data()/size() dependency).
      const std::size_t entities_alive = CountAliveEntities(r);
      ImGui::Text("Entities (alive): %zu", entities_alive);
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
