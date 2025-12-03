// src/core/ecs/World.cpp  -- fixed
#include "World.h"
#include "Tags.h"                // comp::Destroy is fully defined here
#include <entt/entt.hpp>
#include <cstddef>               // std::size_t

using namespace ecs;

// ---- EnTT compatibility: call registry.reserve(size) only if available ----
namespace {
    template <class Registry>
    auto try_reserve(Registry& r, std::size_t n, int)
        -> decltype(r.reserve(n), void()) { r.reserve(n); }
    template <class Registry>
    void try_reserve(Registry&, std::size_t, long) {} // no-op fallback
}

World::World() {
    // Reserve some storage to avoid early reallocations (only if this EnTT supports it)
    try_reserve(reg_, 4096, 0);

    // Alternatively, pre-reserve specific component pools:
    // reg_.storage<YourComponent>().reserve(4096);
}

void World::begin_frame() {
    ++frameIndex_;
    // Clear one-frame tags
    if (auto view = reg_.view<tag::NewlySpawned>()) {
        for (auto e : view) reg_.remove<tag::NewlySpawned>(e);
    }
}

void World::end_frame() {
    // Apply deferred commands
    cmd_.apply(reg_);

    // Garbage: entities tagged for destruction
    if (auto view = reg_.view<comp::Destroy>()) {
        reg_.destroy(view.begin(), view.end());   // range destroy = idiomatic & efficient
    }
}
