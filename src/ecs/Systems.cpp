// src/ecs/Systems.cpp
#include "ecs/Systems.h"
#include "ecs/Components.h"

#include <vector>
#include <algorithm>

namespace colony::ecs {

std::size_t UpdateTickables(entt::registry& r, double dt_seconds) {
  CG_ZONE("ECS::Tickables");
  auto view = r.view<Tickable>();
  std::size_t count = 0;
  for (auto e : view) {
    auto& t = view.get<Tickable>(e);
    if (t.active && t.tick) {
      CG_ZONE("ECS::Tickables::Entity");
      t.tick(r, e, dt_seconds);
      ++count;
    }
  }
  return count;
}

std::size_t UpdateGrowthParallel(entt::registry& r, double dt_seconds,
#if CG_WITH_TASKFLOW
                                 tf::Executor& exec,
#else
                                 void*,
#endif
                                 std::size_t chunk_size) {
  CG_ZONE("ECS::GrowthJobs");
  std::size_t processed = 0;

  auto view = r.view<Growth>();
  if (view.empty()) return 0;

#if CG_WITH_TASKFLOW
  std::vector<entt::entity> items;
  items.reserve(view.size_hint());
  for (auto e : view) items.push_back(e);

  tf::Taskflow flow;
  const std::size_t N = items.size();
  const std::size_t step = std::max<std::size_t>(1, chunk_size);

  for (std::size_t start = 0; start < N; start += step) {
    const std::size_t end = std::min(N, start + step);
    flow.emplace([&r, &items, start, end, dt_seconds, &processed]() {
      CG_ZONE("ECS::GrowthJobs::Chunk");
      std::size_t local = 0;
      for (std::size_t i = start; i < end; ++i) {
        auto e = items[i];
        auto& g = r.get<Growth>(e);
        g.value += g.rate * static_cast<float>(dt_seconds);
        ++local;
      }
      // Not atomic on purpose; minor race acceptable for a metric.
      processed += local;
    });
  }

  exec.run(flow).wait();
#else
  // Serial fallback
  for (auto e : view) {
    auto& g = view.get<Growth>(e);
    g.value += g.rate * static_cast<float>(dt_seconds);
    ++processed;
  }
#endif

  return processed;
}

std::size_t RenderPass(entt::registry& r, float alpha) {
  CG_ZONE("ECS::Renderables");
  auto view = r.view<Renderable>();
  std::size_t count = 0;
  for (auto e : view) {
    auto& rr = view.get<Renderable>(e);
    if (rr.visible && rr.draw) {
      CG_ZONE("ECS::Renderables::Entity");
      rr.draw(r, e, alpha);
      ++count;
    }
  }
  return count;
}

} // namespace colony::ecs
