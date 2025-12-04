#pragma once
// ============================================================================
// SettlementConnector.hpp — auto-route tracks from settlement centers
//  → nearest water access (shoreline) and into RoadNetworkGenerator.
// ============================================================================
//
// Windows macro hygiene (prevents min/max/ERROR macro breakage in consumers)
#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX          // keep std::min/std::max usable
  #endif
  #ifndef NOGDI
  #define NOGDI             // avoid GDI's ERROR macro collisions
  #endif
#endif

// Self-contained STL includes (keep the header independent of PCH order)
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

// Shared worldgen types & utilities (single source of truth)
#include "worldgen/Types.hpp"          // I2, Polyline
#include "worldgen/Common.hpp"         // index3, inb, clamp01

// If you export types from the road generator in the public API,
// keep this include; otherwise forward-declare in a small fwd header.
#include "worldgen/RoadNetworkGenerator.hpp"

namespace worldgen {

// ----------------------------- public types -----------------------------

struct ConnectorParams {
    int   width  = 0;
    int   height = 0;

    // Cost surface (center→shore tracks)
    float slope_weight        = 6.5f;        // cost += slope^2 * slope_weight
    float water_step_penalty  = 50.0f;       // large → avoid stepping into water
    float diagonal_cost       = 1.41421356f; // 8-neigh step cost

    // Post-process (for short tracks to water)
    float rdp_epsilon         = 0.75f;       // Douglas–Peucker epsilon (cells)
    int   chaikin_refinements = 1;           // 0..2 typical for short tracks

    // When building hubs from an existing road_mask, sample a subset:
    int   road_hub_stride     = 8;           // pick every Nth road cell as a hub
};

struct WaterAccess {
    I2       landing{};        // land cell adjacent to water that path touches
    I2       nearest_shore{};  // nearest water-adjacent land cell
    int      path_len_cells = 0;
    Polyline path;             // center → landing
};

struct ConnectorResult {
    // Shoreline masks
    std::vector<std::uint8_t> land_shore_mask;   // W*H (land cells touching water)
    std::vector<std::uint8_t> water_shore_mask;  // W*H (water cells touching land)

    // For each center
    std::vector<WaterAccess> to_water;

    // Output from RoadNetworkGenerator (centers routed into the network)
    RoadResult roads;

    // Convenience: merged mask = short tracks + road network
    std::vector<std::uint8_t> merged_path_mask;  // W*H
};

// ----------------------------- public API --------------------------------
//
// NOTE: This header is declaration-only to avoid header-emitted function
// bodies triggering /WX warnings (e.g., C4505) in non-caller translation units.
// The implementation lives in src/worldgen/SettlementConnector.cpp.
//
// Compute tracks from each settlement center to nearest shoreline, then
// route centers into the global road network using your RoadNetworkGenerator.
ConnectorResult ConnectSettlementsToWaterAndRoads(
    // terrain & masks
    const std::vector<float>&          height01,            // size W*H, 0..1
    int                                W,
    int                                H,
    const std::vector<std::uint8_t>&   water_mask,          // size W*H, 1=water

    // settlements
    const std::vector<I2>&             settlement_centers,

    // optional layers (no defaults here to satisfy default-argument ordering)
    const std::vector<std::uint8_t>*   existing_road_mask,  // optional (may be nullptr)
    const std::vector<float>*          river_order01,       // optional (0..1, may be nullptr)

    // params (defaults appear at the end only)
    const ConnectorParams&             CP_in = {},
    const RoadParams&                  RP_in = {}
);

} // namespace worldgen
