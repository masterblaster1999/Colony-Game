#pragma once
// Minimal agent tag/identity component for ECS entities.
// Kept as plain data so it can be trivially stored and moved.

#include <cstdint>

namespace game::components {

struct Agent {
    std::uint32_t id{0};
    // Expand later with small POD flags/counters if needed (health, faction, etc.).
};

} // namespace game::components
