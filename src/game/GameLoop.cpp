#include "Game.hpp"
#include "GameSystems.hpp"

#include <spdlog/spdlog.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_FRAME() FrameMark
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_FRAME()
  #define COLONY_TRACY_ZONE(name_literal)
#endif

namespace colony {

void Game::Tick(double dt_seconds) {
  if (!m_running.load(std::memory_order_relaxed)) {
    return;
  }

  COLONY_TRACY_ZONE("Game::Tick");

  // --- Advance time --------------------------------------------------------
  m_time.dt_seconds       = dt_seconds;
  m_time.time_since_start += dt_seconds;
  m_time.frame_index++;

  // --- Gather input --------------------------------------------------------
  std::vector<InputEvent> events;
  processInputQueue(events);

  {
    COLONY_TRACY_ZONE("ProcessInput");
    ProcessInput(m_registry, events);
  }

  // --- Simulation ----------------------------------------------------------
  {
    COLONY_TRACY_ZONE("UpdateSimulation");
    UpdateSimulation(m_registry, m_time, *m_executor, *m_taskflow);
  }

  // --- Render --------------------------------------------------------------
  {
    COLONY_TRACY_ZONE("RenderFrame");
    RenderFrame(m_registry, m_time);
  }

  // Mark the end of the frame for Tracy’s timeline
  COLONY_TRACY_FRAME();
}

} // namespace colony
