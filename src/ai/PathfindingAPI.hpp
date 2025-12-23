#pragma once
// src/ai/PathfindingAPI.hpp
//
// Canonical include for lightweight gameplay pathfinding helpers.
//
// Why this file exists:
// - Avoid ambiguous includes like "Pathfinding.hpp" from different folders.
// - Provide a stable place to include both the minimal A* API and the optional
//   time-sliced/path-smoothing Pathfinder.

#include "ai/Pathfinding.hpp"
#include "ai/TimeSlicedPathfinder.hpp"

// -----------------------------------------------------------------------------
// Aliases
// -----------------------------------------------------------------------------
namespace cg::pf {
    // Minimal A* API (defined in ai/Pathfinding.hpp)
    using ::Point;
    using ::GridView;
    using ::PFResult;
    using ::aStar;
}

// Optional time-sliced Pathfinder API (defined in ai/TimeSlicedPathfinder.hpp)
namespace cg::pf_ts {
    using ::ai::PFPoint;
    using ::ai::Path;
    using ::ai::PathRequest;
    using ::ai::Pathfinder;
}
