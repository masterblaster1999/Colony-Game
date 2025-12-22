// include/worldgen/HeightField.hpp
#pragma once

// Public wrapper header:
//   #include <worldgen/HeightField.hpp>
//
// Canonical implementation lives in src/worldgen.

#if defined(__has_include)
  #if __has_include("src/worldgen/HeightField.hpp")
    #include "src/worldgen/HeightField.hpp"
  #else
    #include "../../src/worldgen/HeightField.hpp"
  #endif
#else
  #include "../../src/worldgen/HeightField.hpp"
#endif
