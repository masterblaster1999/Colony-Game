#pragma once
// Unit/scale helpers for worldgen. Header-only, pure inlines.
// Depend only on StagesTypes.hpp.

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "StagesTypes.hpp"

namespace colony::worldgen {

// -----------------------------------------------------------------------------
// Tile <-> meter conversions
// -----------------------------------------------------------------------------

// Canonical: get physical span of a tile in meters (from params).
[[nodiscard]] inline constexpr float tile_span_meters_of(const StageParams& p) {
    return p.tileSizeMeters;
}

// Convenience: read from context directly.
[[nodiscard]] inline constexpr float tile_span_meters_of(const StageContext& ctx) {
    return tile_span_meters_of(ctx.params);
}

// Deleted no-arg form to catch old call-sites early and produce a clear error.
// If you *really* need implicit globals, define a wrapper in your TU.
// (Uncomment to force compile-time guidance)
// float tile_span_meters_of() = delete;

// Convert a physical distance (meters) to "map units" using params scale.
[[nodiscard]] inline constexpr float meters_to_map_units(float meters, const StageParams& p) {
    return meters * p.mapUnitsPerMeter;
}
[[nodiscard]] inline constexpr float meters_to_map_units(float meters, const StageContext& ctx) {
    return meters_to_map_units(meters, ctx.params);
}

// Convert map units back to meters.
[[nodiscard]] inline constexpr float map_units_to_meters(float mu, const StageParams& p) {
    return safe_div(mu, p.mapUnitsPerMeter, /*fallback*/0.0f);
}
[[nodiscard]] inline constexpr float map_units_to_meters(float mu, const StageContext& ctx) {
    return map_units_to_meters(mu, ctx.params);
}

// Number of tiles per meter and meters per tile (explicit names)
[[nodiscard]] inline constexpr float tiles_per_meter(const StageParams& p) {
    return safe_div(1.0f, p.tileSizeMeters, 0.0f);
}
[[nodiscard]] inline constexpr float meters_per_tile(const StageParams& p) {
    return tile_span_meters_of(p);
}

// -----------------------------------------------------------------------------
// Grid indexing helpers (keep small; avoid dragging big headers)
// -----------------------------------------------------------------------------

// Convert 2D tile coordinate to linear index (row-major).
[[nodiscard]] inline constexpr std::int32_t tile_index(std::int32_t x, std::int32_t y, const GridDims& d) {
    return (y * d.width) + x;
}

// Clamp an integer coordinate to grid.
[[nodiscard]] inline constexpr std::int32_t clampi(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// Check if tile coordinate is inside grid.
[[nodiscard]] inline constexpr bool in_bounds(std::int32_t x, std::int32_t y, const GridDims& d) {
    return (x >= 0 && y >= 0 && x < d.width && y < d.height);
}

// -----------------------------------------------------------------------------
// Small numeric helpers commonly used in stages (keep header-only).
// -----------------------------------------------------------------------------

[[nodiscard]] inline float lerp(float a, float b, float t) {
    return std::fma((b - a), t, a);
}

[[nodiscard]] inline float remap(float v, float inMin, float inMax, float outMin, float outMax) {
    const float t = safe_div(v - inMin, (inMax - inMin), 0.0f);
    return lerp(outMin, outMax, std::clamp(t, 0.0f, 1.0f));
}

// Convert from "tiles" to meters and map units (handy overloads).
[[nodiscard]] inline constexpr float tiles_to_meters(float tiles, const StageParams& p) {
    return tiles * p.tileSizeMeters;
}
[[nodiscard]] inline constexpr float tiles_to_map_units(float tiles, const StageParams& p) {
    return meters_to_map_units(tiles_to_meters(tiles, p), p);
}

} // namespace colony::worldgen
