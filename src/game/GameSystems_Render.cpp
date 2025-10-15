#include "GameSystems.hpp"
#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_ZONE(name_literal)
#endif

#ifdef COLONY_ENABLE_IMGUI
  #include <imgui.h>
#endif

namespace colony {

void RenderFrame(entt::registry& r, const GameTime& gt) {
  COLONY_TRACY_ZONE("RenderFrame");

  // Your existing renderer should be invoked here (D3D11, etc.).
  // This file intentionally avoids coupling to DirectX types.

#ifdef COLONY_ENABLE_IMGUI
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Colony HUD")) {
      ImGui::Text("Frame: %llu", static_cast<unsigned long long>(gt.frame_index));
      ImGui::Text("dt (ms): %.3f", gt.dt_seconds * 1000.0);
      ImGui::Text("t (s):  %.3f", gt.time_since_start);
      ImGui::Separator();
      // Lightweight metrics
      ImGui::Text("Entities: %zu", static_cast<size_t>(r.alive()));
    }
    ImGui::End();
  }
#endif
}

} // namespace colony
