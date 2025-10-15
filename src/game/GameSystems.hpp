#pragma once

#include <cstdint>
#include <vector>
#include <entt/entt.hpp>

#include "Game.hpp" // for InputEvent & GameTime

namespace tf {
  class Executor;
  class Taskflow;
}

namespace colony {

// Split system APIs kept small and testable.
void ProcessInput(entt::registry& r, const std::vector<InputEvent>& events);

void UpdateSimulation(entt::registry& r,
                      const GameTime& gt,
                      tf::Executor& executor,
                      tf::Taskflow& taskflow);

void RenderFrame(entt::registry& r, const GameTime& gt);

} // namespace colony
