#pragma once
// Minimal velocity component for ECS systems.
// Kept as plain data (no virtuals, no heavy helpers) for cache efficiency.
// Typical usage: systems read/write x,y each tick to integrate motion.

namespace game::components {

struct Velocity {
    float x{0.0f};
    float y{0.0f};
};

} // namespace game::components
