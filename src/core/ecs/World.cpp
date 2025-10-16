#include "World.h"
#include "Tags.h"
#include <entt/entt.hpp>

using namespace ecs;

World::World() {
    // Reserve some storage to avoid early reallocations
    reg_.reserve(4096);
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
        for (auto e : view) reg_.destroy(e);
    }
}
