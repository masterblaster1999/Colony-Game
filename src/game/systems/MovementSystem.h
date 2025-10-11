#pragma once
#include <entt/entt.hpp>
#include "game/components/Transform.h"
#include "game/components/Velocity.h"

// Keep this in the global namespace to match the current test usage.
struct MovementSystem {
    static void update(entt::registry& reg, float dt) {
        auto view = reg.view<game::components::Transform, game::components::Velocity>();
        for (auto e : view) {
            auto& t = view.get<game::components::Transform>(e);
            const auto& v = view.get<game::components::Velocity>(e);
            t.x += v.x * dt;
            t.y += v.y * dt;
        }
    }
};
