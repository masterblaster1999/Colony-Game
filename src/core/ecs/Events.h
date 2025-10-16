#pragma once
#include <entt/entt.hpp>
#include <cstdint>

namespace evt {
    struct RequestBuild { uint32_t buildingType; float x, y, z; };
    struct ConstructionCompleted { entt::entity building; };
    struct ResourceDepleted    { entt::entity node; };
}
