// File: src/game/GameSystems_Simulation.h
//
// Back‑compat shim: this header used to (or may still) be included by code
// that expects simulation system declarations here. The real declarations
// live in one of the engine GameSystems headers. This shim forwards to the
// best available header and provides a helpful error if neither is present.

#pragma once

// Prefer the dedicated simulation declarations header if present.
#if __has_include("game/GameSystems_Simulation.hpp")
  #include "game/GameSystems_Simulation.hpp"

// Fallback to the consolidated systems header used in newer layouts.
#elif __has_include("game/GameSystems.hpp")
  #include "game/GameSystems.hpp"

#else
  #error "GameSystems_Simulation.h: could not locate 'game/GameSystems_Simulation.hpp' \
or 'game/GameSystems.hpp'. Verify your include paths and file names."
#endif
