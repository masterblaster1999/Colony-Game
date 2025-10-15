#include "GameSystems.hpp"

#include <spdlog/spdlog.h>
#include <taskflow/taskflow.hpp>  // implementation here, not in headers

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
  #define COLONY_TRACY_ZONE(name_literal) ZoneScopedN(name_literal)
#else
  #define COLONY_TRACY_ZONE(name_literal)
#endif

namespace colony {

void UpdateSimulation(entt::registry& r,
                      const GameTime& gt,
                      tf::Executor& executor,
                      tf::Taskflow& taskflow) {
  COLONY_TRACY_ZONE("UpdateSimulation");

  taskflow.clear(); // reuse the same object to avoid allocations

  // Sketch of parallel frame jobs; replace with your real systems.
  auto pre   = taskflow.emplace([&](){
                  COLONY_TRACY_ZONE("PreFrame");
                  // e.g., handle spawning/despawning, streaming, events
                }).name("PreFrame");

  auto ai    = taskflow.emplace([&](){
                  COLONY_TRACY_ZONE("AI");
                  // iterate AI components, behavior trees, etc.
                  (void)r;
                }).name("AI");

  auto sim   = taskflow.emplace([&](){
                  COLONY_TRACY_ZONE("Simulation");
                  // physics/integration/colonist updates using gt.dt_seconds
                  (void)r; (void)gt;
                }).name("Simulation");

  auto jobs  = taskflow.emplace([&](){
                  COLONY_TRACY_ZONE("Jobs");
                  // fire-and-forget parallel_for example:
                  // (Replace with actual registry iteration)
                  const int N = 1000;
                  tf::Taskflow inner;
                  inner.for_each_index(0, N, 1, [&](int /*i*/){
                    // per-entity small work
                  });
                  tf::Executor inner_exec;
                  inner_exec.run(inner).wait();
                }).name("Jobs");

  auto post  = taskflow.emplace([&](){
                  COLONY_TRACY_ZONE("PostFrame");
                  // finalize frame: commit changes to render state, etc.
                }).name("PostFrame");

  pre.precede(ai, sim, jobs);
  ai.precede(post);
  sim.precede(post);
  jobs.precede(post);

  executor.run(taskflow).wait();
}

} // namespace colony
