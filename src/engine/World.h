// src/engine/World.h
#pragma once
#include <entt/entt.hpp>

#if __has_include(<taskflow/taskflow.hpp>)
  #include <taskflow/taskflow.hpp>
  #define CG_WITH_TASKFLOW 1
#else
  #define CG_WITH_TASKFLOW 0
#endif

namespace colony {

struct World {
  entt::registry registry;

#if CG_WITH_TASKFLOW
  // Shared task executor for parallel jobs
  tf::Executor   jobs;
#endif

  double sim_time_seconds = 0.0;

  void reset() {
    registry.clear();
    sim_time_seconds = 0.0;
  }
};

} // namespace colony
