#include "World.h"
#include "Tags.h"
#include <entt/entt.hpp>
#include <cstddef> // std::size_t

// If the Destroy tag isn't brought in by Tags.h, a forward declaration is
// sufficient here because we only use it as a tag (no member access).
namespace comp { struct Destroy; }

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

    // If you want to pre-reserve specific component pools instead (portable across EnTT):
    // reg_.storage<YourComponent>().reserve(4096);
    // reg_.storage<AnotherComponent>().reserve(4096);
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
        reg_.destroy(view.begin(), view.end()); // range overload is concise & efficient
    }
}
