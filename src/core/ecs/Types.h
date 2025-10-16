#pragma once
#include <cstdint>
#include <entt/entt.hpp>

namespace ecs {
    using Registry = entt::registry;
    using Entity   = entt::entity;
    constexpr Entity Null = entt::null;

    // Stable, explicit duration/time types
    using seconds_f = float;
}
