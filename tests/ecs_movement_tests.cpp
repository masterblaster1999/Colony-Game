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
    // If your Transform has different member names (e.g., pos.x), adjust the two lines below.
    auto& t0 = reg.emplace<Transform>(e);
    t0.position.x = 1.0f;
    t0.position.y = 2.0f;

    // Velocity lives in game::components.
    reg.emplace<game::components::Velocity>(e, game::components::Velocity{ 3.0f, -1.0f });

    // MovementSystem is namespaced under game::systems and has an instance `update(reg, dt)`.
    game::systems::MovementSystem sys;
    sys.update(reg, 0.5f); // half a second

    const auto& t = reg.get<Transform>(e);
    CHECK(t.position.x == doctest::Approx(2.5f)); // 1 + 3*0.5
    CHECK(t.position.y == doctest::Approx(1.5f)); // 2 + (-1)*0.5
}
