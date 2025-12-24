#pragma once
// Nav2D.hpp - single-header navigation for colony sims
// C++17+, header-only. Drop into src/navigation/Nav2D.hpp and add src/ to your include path.
// Features (toggle with macros below):
// - Grid with dynamic block/cost + revision counter (serialization support)
// - A* (4/8-way), no-corner-cutting option
// - Jump Point Search (JPS) for uniform-cost grids
// - LRU path cache (keyed by start/goal/flags + grid revision)
// - LOS smoothing
// - Deterministic tie-breaking; optional seeded randomness for tiebreaks in crowd rules
// - Flow fields: single and multi-source, blending with scalar hazard fields, gradient sampling
// - D* Lite incremental re-planning on terrain change (notify deltas for O(k log n))
// - HPA*: clusters + portals + abstract search + stitching (rebuild on revision threshold)
// - Simple grid-density crowd avoidance with lateral bias
// - Debug: ASCII dumps for paths/flow; optional ImGui overlay hooks
// - Lightweight profiling counters (expansions, heap ops, cache hits)
// - Header-embedded tests under NAV2D_TESTS
//
// This file is intentionally self-contained. You can enable/disable blocks by setting macros
// before including the header or via compiler flags (-DNAME=VALUE).

#include <vector>
#include <queue>
#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <cmath>
#include <algorithm>
#include <functional>
#include <cassert>
#include <optional>
#include <utility>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>

//////////////////////////////////////////////////////////////
// Feature flags (override with -D on your build if desired)
#ifndef NAV2D_ENABLE_JPS
#define NAV2D_ENABLE_JPS 1
#endif
#ifndef NAV2D_ENABLE_CACHE
#define NAV2D_ENABLE_CACHE 1
#endif
#ifndef NAV2D_ENABLE_FLOWFIELD
#define NAV2D_ENABLE_FLOWFIELD 1
#endif
#ifndef NAV2D_ENABLE_DSTARLITE
#define NAV2D_ENABLE_DSTARLITE 1
#endif
#ifndef NAV2D_ENABLE_HPA
#define NAV2D_ENABLE_HPA 1
#endif
#ifndef NAV2D_ENABLE_DEBUG
#define NAV2D_ENABLE_DEBUG 1
#endif
#ifndef NAV2D_ENABLE_IMGUI
#define NAV2D_ENABLE_IMGUI 0
#endif
#ifndef NAV2D_DETERMINISTIC
#define NAV2D_DETERMINISTIC 1
#endif
#ifndef NAV2D_CACHE_CAPACITY
#define NAV2D_CACHE_CAPACITY 128
#endif
#ifndef NAV2D_SEED
#define NAV2D_SEED 0xC0FFEE1234ULL
#endif

#if NAV2D_ENABLE_IMGUI
#include <imgui.h>
#endif

namespace nav2d {


// NOTE: This header has been split into smaller *.inl parts under src/navigation/detail/
//       to improve readability while remaining header-only.

#include "detail/Nav2D_helpers.inl"
#include "detail/Nav2D_rng.inl"
#include "detail/Nav2D_grid.inl"
#include "detail/Nav2D_dstar.inl"
#include "detail/Nav2D_planner.inl"

} // namespace nav2d

#ifdef NAV2D_TESTS
#include "detail/Nav2D_tests.inl"
#endif // NAV2D_TESTS
