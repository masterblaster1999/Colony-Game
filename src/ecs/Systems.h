// src/ecs/Systems.h
#pragma once
#include <cstddef>
#include <entt/entt.hpp>

#include "core/Profile.h"

#if __has_include(<taskflow/taskflow.hpp>)
  #include <taskflow/taskflow.hpp>
  #define CG_WITH_TASKFLOW 1
#else
  #define CG_WITH_TASKFLOW 0
#endif

namespace colony::ecs {

// Returns number of entities processed.
std::size_t UpdateTickables(entt::registry& r, double dt_seconds);

// Parallel "jobs" example: updates Growth in chunks.
// If Taskflow isn't available, falls back to serial.
std::size_t UpdateGrowthParallel(entt::registry& r, double dt_seconds,
#if CG_WITH_TASKFLOW
                                 tf::Executor& exec,
#else
                                 void* /*unused*/,
#endif
                                 std::size_t chunk_size = 512);

// Returns number of entities drawn.
std::size_t RenderPass(entt::registry& r, float alpha);

} // namespace colony::ecs
