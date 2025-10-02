#pragma once
// Umbrella header for legacy includes: include this in old TUs.
// Splits heavy code into small, cache-friendly headers while preserving names.

#include "StagesTypes.hpp"
#include "StagesUnits.hpp"

namespace colony::worldgen {

// -----------------------------------------------------------------------------
// Back-compat *overloads* (safe shims)
// -----------------------------------------------------------------------------
//
// Many call-sites previously did:
//   const float s = tile_span_meters_of();                // (no args) ❌
//   auto mu = meters_to_map_units(tile_span_meters_of()); // ❌
//
// New API wants explicit params or context. These shims keep *correct*
// usage easy to migrate while still being unambiguous.
//

// Prefer passing the StageContext explicitly everywhere:
using colony::worldgen::tile_span_meters_of;
using colony::worldgen::meters_to_map_units;

// Assist migration: if a call-site already has `ctx`, these overloads resolve.
// Declarations only — definitions live in a single header/implementation unit.
[[nodiscard]] float tile_span_meters_of(const StageContext& ctx);
[[nodiscard]] float meters_to_map_units(float meters, const StageContext& ctx);

// Intentionally NO zero-arg versions here. If you *must* keep them compiling
// temporarily, create a TU-local wrapper that fetches params from your
// world singletons and call the new functions above.

} // namespace colony::worldgen
