#pragma once
#include <cstdint>
#include <entt/entt.hpp>

namespace comp {

struct Colonist { uint32_t id; };  // stable external id (for UI/save)

struct Inventory {
    // keep tiny for cache; expand with a separate store if needed
    uint16_t wood{}, stone{}, food{};
    uint16_t capacity{100};
};

enum class JobType : uint8_t { None, Build, Mine, Haul };

struct JobSeeker { /* tag: eligible for job assignment */ };

struct AssignedJob {
    JobType type{JobType::None};
    entt::entity target{entt::null};   // entity to work on (building, node)
};

struct NavAgent {
    // next waypoint; path is kept elsewhere to avoid per-entity huge vectors
    float targetX{}, targetY{}, targetZ{};
    float speed{3.0f};
    bool  hasTarget{false};
};
}
