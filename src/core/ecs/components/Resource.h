#pragma once
#include <cstdint>
#include <entt/entt.hpp>

namespace comp {

enum class ResourceKind : uint8_t { Wood, Stone, Food };

struct ResourceNode {
    ResourceKind kind{ResourceKind::Wood};
    uint32_t amount{1000};
};
}
