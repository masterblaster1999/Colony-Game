// src/ecs/Systems.cpp
#include "ecs/Systems.h"
#include "ecs/Components.h"

#include <vector>
#include <algorithm>   // max, min
#include <iterator>    // std::distance
#if CG_WITH_TASKFLOW
  #include <atomic>    // std::atomic_size_t
#endif

namespace colony::ecs {

std::size_t UpdateTickables(entt::registry& r, double dt_seconds) {
    CG_ZONE("ECS::Tickables");

    auto view = r.view<Tickable>();
    std::size_t count = 0;

    // Use view.each() to avoid repeated lookups and keep things tidy.
    for (auto [e, t] : view.each()) {
        if (t.active && t.tick) {
            CG_ZONE("ECS::Tickables::Entity");
            t.tick(r, e, dt_seconds);
            ++count;
        }
    }
    return count;
}

std::size_t UpdateGrowthParallel(entt::registry& r,
                                 double dt_seconds,
#if CG_WITH_TASKFLOW
                                 tf::Executor& exec,
#else
                                 void* /*unused*/,
#endif
                                 std::size_t chunk_size) {
    CG_ZONE("ECS::GrowthJobs");

    auto view = r.view<Growth>();
    if (view.begin() == view.end()) {
        return 0;
    }

    const float dtf = static_cast<float>(dt_seconds);

#if CG_WITH_TASKFLOW
    // Atomic metric to avoid a data race in parallel region.
    std::atomic_size_t processed{0};

    // Portable reservation for EnTT views: size_hint() is only available
    // when deletion_policy == in_place; use iterator distance instead.
    // Ref: entt::basic_storage_view::size_hint SFINAE docs. 
    // (https://skypjack.github.io/entt/classentt_1_1basic__storage__view.html)
    std::vector<entt::entity> items;
    const auto approx = static_cast<std::size_t>(std::distance(view.begin(), view.end()));
    items.reserve(approx);
    for (auto e : view) items.push_back(e);

    tf::Taskflow flow;
    const std::size_t N = items.size();
    const std::size_t step = std::max<std::size_t>(1, chunk_size);

    // Assumption: no structural changes to the registry for the duration
    // of this pass; we only write to Growth components of distinct entities.
    for (std::size_t start = 0; start < N; start += step) {
        const std::size_t end = std::min<std::size_t>(N, start + step);
        flow.emplace([&r, &items, start, end, dtf, &processed]() {
            CG_ZONE("ECS::GrowthJobs::Chunk");
            std::size_t local = 0;
            for (std::size_t i = start; i < end; ++i) {
                const entt::entity e = items[i];
                auto& g = r.get<Growth>(e);
                g.value += g.rate * dtf;
                ++local;
            }
            processed.fetch_add(local, std::memory_order_relaxed);
        });
    }

    exec.run(flow).wait();
    return processed.load(std::memory_order_relaxed);
#else
    // Serial fallback: use view.each() to avoid extra lookups.
    std::size_t processed = 0;
    for (auto [e, g] : view.each()) {
        (void)e; // e is currently unused in this loop
        g.value += g.rate * dtf;
        ++processed;
    }
    return processed;
#endif
}

std::size_t RenderPass(entt::registry& r, float alpha) {
    CG_ZONE("ECS::Renderables");

    auto view = r.view<Renderable>();
    std::size_t count = 0;

    for (auto [e, rr] : view.each()) {
        if (rr.visible && rr.draw) {
            CG_ZONE("ECS::Renderables::Entity");
            rr.draw(r, e, alpha);
            ++count;
        }
    }
    return count;
}

} // namespace colony::ecs
