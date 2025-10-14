// tests/ecs_movement_tests.cpp
#include <doctest/doctest.h>
#include <entt/entt.hpp>

#include "game/components/Transform.h"
#include "game/components/Velocity.h"
#include "game/systems/MovementSystem.h"

TEST_CASE("movement integrates position") {
    entt::registry reg;
    const entt::entity e = reg.create();

    // Create a Transform and initialize to (1, 2).
    // The fallback Transform stores position as a float[3], so index into it.
    auto& t0 = reg.emplace<Transform>(e);
    t0.position[0] = 1.0f; // x
    t0.position[1] = 2.0f; // y

    // Velocity lives in game::components.
    reg.emplace<game::components::Velocity>(e, game::components::Velocity{ 3.0f, -1.0f });

    // MovementSystem is namespaced under game::systems and has an instance `update(reg, dt)`.
    game::systems::MovementSystem sys;
    sys.update(reg, 0.5f); // half a second

    const auto& t = reg.get<Transform>(e);
    CHECK(t.position[0] == doctest::Approx(2.5f)); // 1 + 3*0.5
    CHECK(t.position[1] == doctest::Approx(1.5f)); // 2 + (-1)*0.5
}
