#pragma once
#include "../World.h"
#include "../components/Common.h"

namespace sys {
    inline void sweep_garbage(ecs::World& world, ecs::seconds_f dt) {
        auto& reg = world.registry();
        auto v = reg.view<comp::Lifetime>();
        for (auto e : v) {
            auto& lf = v.get<comp::Lifetime>(e);
            lf.remaining -= dt;
            if (lf.remaining <= 0) {
                world.commands().emplace<comp::Destroy>(e);
            }
        }
    }
}
