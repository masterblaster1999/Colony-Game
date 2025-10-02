#pragma once
// Lightweight types used by worldgen "stages" code.
// Keep this header tiny and stable to avoid ODR pain and long rebuilds.

#include <cstdint>
#include <utility>

namespace colony::worldgen {

// Dimensions of the world/tile grid (in tiles).
struct GridDims {
    std::int32_t width  = 0;  // tiles
    std::int32_t height = 0;  // tiles
};

// Parameters that affect spatial scale and conversions.
// KEEP: header-only, trivially-copyable; used widely in constexpr/inlines.
struct StageParams {
    // Size of one gameplay tile in meters (physical scale of a tile).
    float tileSizeMeters = 1.0f;

    // Optional "map unit" scale (virtual units per meter). If your codebase
    // already defines a global convention, leave this at 1.0f.
    float mapUnitsPerMeter = 1.0f;

    // Grid dimensions (if convenient to carry here).
    GridDims grid{};
};

// NOTE: The real StageContext lives in worldgen/StageContext.hpp.
// Do not define a second StageContext here; it causes ODR/type mismatches.

// Utility for constexpr-safe division.
[[nodiscard]] inline constexpr float safe_div(float num, float den, float fallback = 0.0f) {
    return (den != 0.0f) ? (num / den) : fallback;
}

} // namespace colony::worldgen
