#include "GameSystems.hpp"
#include <spdlog/spdlog.h>
#include <cstddef>          // std::size_t
#include <entt/entt.hpp>    // entt::registry::{valid,data,size}

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

// Robust, version‑stable alive‑entity count for EnTT.
//
// Strategy:
//  1) If the installed EnTT exposes registry.alive(), use it (SFINAE).
//  2) Otherwise, walk [registry.data(), registry.data()+registry.size())
//     and filter with registry.valid(id). This is O(N) and perfectly fine
//     for HUD/debug; it avoids relying on removed/renamed APIs.
//
// Docs:
//  - `valid(entity)` is the supported way to test a handle. :contentReference[oaicite:1]{index=1}
//  - `data()` exposes the backing array (valid + destroyed); pair it with
//    `valid()` before use. `size()` returns the length of that array. :contentReference[oaicite:2]{index=2}
template <class R>
static auto TryAliveCount(const R& reg, int)
  -> decltype(static_cast<std::size_t>(reg.alive()))   // prefers this if available
{
  return static_cast<std::size_t>(reg.alive());
}

template <class R>
static std::size_t TryAliveCount(const R& reg, long)    // fallback
{
  std::size_t alive = 0;
  const auto* const ids = reg.data();
  const std::size_t n   = static_cast<std::size_t>(reg.size());
  for(std::size_t i = 0; i < n; ++i) {
    if (reg.valid(ids[i])) { ++alive; }
  }
  return alive;
}

inline std::size_t CountAliveEntities(const entt::registry& reg) {
  // Prefer reg.alive() if it exists on this EnTT version; otherwise use the
  // data()+size()+valid() fallback above.
  return TryAliveCount(reg, 0);
}

#if COLONY_HAVE_IMGUI
// Simple exponential moving average for FPS to reduce jitter in the HUD.
struct FpsEma {
  float ema   = 0.0f;
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

      // Version-stable alive count (no registry.alive() dependency).
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
