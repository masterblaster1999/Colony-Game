#pragma once
#include "../World.h"
#include "../Events.h"
#include "../components/Building.h"
#include "../components/Colonist.h"
#include "../components/Common.h"

namespace sys {
    inline void update_construction(ecs::World& world, ecs::seconds_f dt) {
        auto& reg = world.registry();

        auto view = reg.view<comp::AssignedJob, comp::Colonist, comp::Inventory>();
        for (auto e : view) {
            auto& job = view.get<comp::AssignedJob>(e);
            if (job.type != comp::JobType::Build || job.target == entt::null) continue;
            if (!reg.valid(job.target) || !reg.any_of<comp::ConstructionSite>(job.target)) continue;

            auto& inv  = view.get<comp::Inventory>(e);
            auto& site = reg.get<comp::ConstructionSite>(job.target);

            // Move 1 unit per tick from inventory to site, prioritize wood
            if (inv.wood && site.woodHave < site.woodNeeded) { inv.wood--; site.woodHave++; }
            else if (inv.stone && site.stoneHave < site.stoneNeeded) { inv.stone--; site.stoneHave++; }

            if (site.complete()) {
                world.dispatcher().trigger(evt::ConstructionCompleted{ job.target });
                // Mark job done
                world.commands().remove<comp::AssignedJob>(e);
                // Mark building operational
                world.commands().emplace<comp::Building>(job.target, comp::Building{ /*type*/ 0, /*operational*/ true });
                world.commands().remove<comp::ConstructionSite>(job.target);
            }
        }
    }
}
