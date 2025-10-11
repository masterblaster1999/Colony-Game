#pragma once
#include <entt/entt.hpp>

// Velocity is in game::components (per your include path in the error)
#include "game/components/Velocity.h"
// Transform currently appears to be in the global namespace (see compiler note).
// Include wherever it's defined; path below matches your error dump.
#include "game/components/Transform.h"  // if this is actually under src/, keep the include as-is to the real header

namespace game::systems {
struct MovementSystem {
    void update(entt::registry& registry, float dt) const {
        // Use the correct EnTT template form.
        // IMPORTANT: Transform is *not* in game::components in your repo.
        auto view = registry.view<Transform, game::components::Velocity>();

        // Safe, portable EnTT loop without structured bindings (keeps MSVC happy and avoids version differences)
        for (auto entity : view) {
            auto& t = view.get<Transform>(entity);
            auto& v = view.get<game::components::Velocity>(entity);

            // Adjust to your Transform layout:
            // If Transform uses e.g. t.position.{x,y} or t.pos.{x,y}, update both lines accordingly.
            // The tests expect x/y movement, so default to position.x/y if available.
            t.position.x += v.x * dt;
            t.position.y += v.y * dt;
        }
    }
};
} // namespace game::systems
