#include "GameSystems.hpp"

#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_ZONE(name_literal)
#endif

namespace colony {

void ProcessInput(entt::registry& r, const std::vector<InputEvent>& events) {
  (void)r; // registry not used in this minimal skeleton
  COLONY_TRACY_ZONE("ProcessInput");

  // Example: react to Quit or dispatch to your existing input system
  for (const auto& e : events) {
    switch (e.type) {
      case InputEventType::Quit:
        // Typically handled in platform layer which will call Game::RequestQuit()
        spdlog::info("Input: Quit requested");
        break;
      case InputEventType::KeyDown:
        spdlog::debug("KeyDown: code={} mods[alt={},ctrl={},shift={}]", e.a, e.alt, e.ctrl, e.shift);
        break;
      case InputEventType::MouseMove:
        // Keep it quiet unless debugging to avoid log spam
        break;
      default:
        break;
    }
  }

  // Hook point: translate events -> EnTT signals or component flags here.
}

} // namespace colony
