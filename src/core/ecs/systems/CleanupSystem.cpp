// src/core/ecs/systems/CleanupSystem.cpp
#include <entt/entity/registry.hpp>
#include "core/ecs/components/Destroy.hpp"

void runCleanup(entt::registry& reg) {
    // batch-destroy all entities that have the Destroy tag
    auto view = reg.view<comp::Destroy>();
    reg.destroy(view.begin(), view.end());        // efficient range destroy
}
