#pragma once
#include <string>
#include <array>
#include <entt/entt.hpp>

namespace comp {

// Human-readable name (debug/inspector)
struct Name { std::string value; };

// World-space transform (column-major minimal set)
struct Transform {
    // Position only for now; rotate/scale later if needed
    float x{}, y{}, z{};
};

// Simple velocity
struct Velocity { float vx{}, vy{}, vz{}; };

// Lifetime / kill-after timer (seconds)
struct Lifetime { float remaining{}; };

// Mark for end-of-frame destruction (deferred)
struct Destroy {};
}
