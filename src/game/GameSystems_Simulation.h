// File: src/game/GameSystems_Simulation.h
//
// Back‑compat shim: this header used to (or may still) be included by code
// that expects simulation system declarations here. The real declarations
// live in one of the engine GameSystems headers. This shim forwards to the
// best available header and also provides minimal no‑op overloads for
// GameSystems::Simulation::{Init,Update} so legacy call sites keep compiling.

#pragma once

#include <cstdint>

// Prefer the dedicated simulation declarations header if present.
#if __has_include("game/GameSystems_Simulation.hpp")
  #include "game/GameSystems_Simulation.hpp"
#endif

// Fallback to the consolidated systems header used in newer layouts.
#if __has_include("game/GameSystems.hpp")
  #include "game/GameSystems.hpp"
#endif

namespace GameSystems {
namespace Simulation {

// -----------------------------------------------------------------------------
// Minimal back‑compat overload set (no‑op):
// These templates accept common call shapes seen across older code (with/without
// seed for Init, with/without dt for Update). If your project provides real
// implementations in included headers above, those non‑template overloads will
// be preferred by overload resolution; these act only as safe stubs.
// -----------------------------------------------------------------------------

// Init: 4‑arg form (no seed)
template <typename WorldT, typename ColonyT, typename ColonistsT, typename HostilesT>
inline void Init(WorldT&, ColonyT&, ColonistsT&, HostilesT&) {}

// Init: 5‑arg form (with seed)
template <typename WorldT, typename ColonyT, typename ColonistsT, typename HostilesT>
inline void Init(WorldT&, ColonyT&, ColonistsT&, HostilesT&, std::uint32_t) {}

// Update: 4‑arg form (no dt)
template <typename WorldT, typename ColonyT, typename ColonistsT, typename HostilesT>
inline void Update(WorldT&, ColonyT&, ColonistsT&, HostilesT&) {}

// Update: 5‑arg form (with dt)
template <typename WorldT, typename ColonyT, typename ColonistsT, typename HostilesT>
inline void Update(WorldT&, ColonyT&, ColonistsT&, HostilesT&, double) {}

} // namespace Simulation
} // namespace GameSystems
