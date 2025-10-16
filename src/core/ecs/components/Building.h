#pragma once
#include <cstdint>
#include <entt/entt.hpp>

namespace comp {

struct Building {
    uint32_t type;      // enum or ID
    bool     operational{false};
};

struct ConstructionSite {
    uint16_t woodNeeded{}, stoneNeeded{};
    uint16_t woodHave{},  stoneHave{};
    bool complete() const {
        return woodHave >= woodNeeded && stoneHave >= stoneNeeded;
    }
};
}
