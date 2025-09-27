// src/worldgen/Math.hpp
#pragma once
#include <algorithm> // clamp

namespace colony::worldgen {

// Single, inline definitions avoid multiple-definition/linkage problems.
inline constexpr float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

inline float smoothstep(float edge0, float edge1, float x) noexcept {
    if (edge0 == edge1) return x <= edge0 ? 0.0f : 1.0f;
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace colony::worldgen
