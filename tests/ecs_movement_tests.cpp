// tests/ecs_movement_tests.cpp
#include <doctest/doctest.h>
#include <entt/entt.hpp>
#include "game/components/Transform.h"
#include "game/components/Velocity.h"
#include "game/systems/MovementSystem.h"

TEST_CASE("movement integrates position") {
    entt::registry reg;
    const auto e = reg.create();
    reg.emplace<Transform>(e, 1.f, 2.f);
    reg.emplace<Velocity>(e, 3.f, -1.f);

    MovementSystem::update(reg, 0.5f); // half a second

    auto& t = reg.get<Transform>(e);
    CHECK(t.x == doctest::Approx(2.5f)); // 1 + 3*0.5
    CHECK(t.y == doctest::Approx(1.5f)); // 2 + (-1)*0.5
}
