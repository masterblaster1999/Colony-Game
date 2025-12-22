// include/worldgen/Hydrology.hpp
#pragma once
#ifndef COLONY_INCLUDE_WORLDGEN_HYDROLOGY_HPP
#define COLONY_INCLUDE_WORLDGEN_HYDROLOGY_HPP

// Public wrapper header.
// This must NEVER include itself (directly or indirectly).
//
// Canonical implementation header is in:
//   src/worldgen/Hydrology.hpp
//
// Keep this wrapper thin and stable so external code can:
//   #include <worldgen/Hydrology.hpp>

#if defined(__has_include)
  #if __has_include("src/worldgen/Hydrology.hpp")
    #include "src/worldgen/Hydrology.hpp"
  #elif __has_include("../../src/worldgen/Hydrology.hpp")
    // Fallback for build systems that don't add repo root to include paths.
    #include "../../src/worldgen/Hydrology.hpp"
  #else
    #error "Cannot find src/worldgen/Hydrology.hpp. Add repo root to include paths or keep the include/ tree intact."
  #endif
#else
  // MSVC supports __has_include, but keep a conservative fallback.
  #include "../../src/worldgen/Hydrology.hpp"
#endif

#endif // COLONY_INCLUDE_WORLDGEN_HYDROLOGY_HPP
