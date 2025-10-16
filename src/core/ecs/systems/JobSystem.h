#pragma once
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Building.h"
#include "../components/Common.h"

namespace sys {

    // Assign jobs to seekers; keep it simple & deterministic
    inline void update_jobs(ecs::World& world) {
        auto& reg = world.registry();

        // Sort seekers & sites once for determinism (do outside hot loop ideally)
        reg.sort<comp::Colonist>([](auto const& a, auto const& b){ return a.id < b.id; });

        auto seekers = reg.view<comp::Colonist, comp::JobSeeker>(entt::exclude<comp::AssignedJob>);
        auto sites   = reg.view<comp::ConstructionSite, comp::Transform>();

        for (auto seeker : seekers) {
            // Pick the first incomplete site (real code: spatial nearest)
            entt::entity chosen = entt::null;
            for (auto s : sites) {
                const auto& site = sites.get<comp::ConstructionSite>(s);
                if (!site.complete()) { chosen = s; break; }
            }
            if (chosen != entt::null) {
                world.commands().emplace<comp::AssignedJob>(seeker, comp::JobType::Build, chosen);
                world.registry().remove<comp::JobSeeker>(seeker);
            } else {
                // no sites; remain a seeker this frame
            }
        }
    }
}
