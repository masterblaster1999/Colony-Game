#pragma once
#include "../World.h"
#include "../components/Common.h"
#include "../components/Colonist.h"

namespace sys {
    inline void update_movement(ecs::World& world, ecs::seconds_f dt) {
        auto& reg = world.registry();
        auto group = reg.group<comp::Transform, comp::Velocity>();
        for (auto [e, tr, vel] : group.each()) {
            tr.x += vel.vx * dt;
            tr.y += vel.vy * dt;
            tr.z += vel.vz * dt;
        }
    }
}
