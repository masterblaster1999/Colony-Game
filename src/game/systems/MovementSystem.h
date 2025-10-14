#pragma once
#include <entt/entt.hpp>

// Velocity is in game::components (per your include path)
#include "game/components/Velocity.h"
// Transform appears to be in the global (or root game) namespace.
// Keep this include path aligned with your repo layout.
#include "game/components/Transform.h"

namespace game::systems {

struct MovementSystem {
    void update(entt::registry& registry, float dt) const {
        // EnTT view over Transform + Velocity
        auto view = registry.view<Transform, game::components::Velocity>();

        // Safe EnTT loop without structured bindings (MSVC-friendly)
        for (auto entity : view) {
            auto& t = view.get<Transform>(entity);
            auto& v = view.get<game::components::Velocity>(entity);

            // Minimal compile fix:
            // Transform::position is a float[3], so use array indexing
            // instead of member access ('.x/.y'), which triggers C2228.
            t.position[0] += v.x * dt;  // X
            t.position[1] += v.y * dt;  // Y

            // If you later extend to 3D and Velocity exposes 'z',
            // uncomment the following line:
            // t.position[2] += v.z * dt; // Z
        }
    }
};

} // namespace game::systems
