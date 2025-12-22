// include/worldgen/Hydrology.hpp
#pragma once

// Public wrapper header to preserve the stable include path:
//   #include <worldgen/Hydrology.hpp>
//
// The implementation header currently lives in src/worldgen.
// This wrapper avoids accidental self-includes (the previous version included itself).

#if defined(__has_include)
  #if __has_include("src/worldgen/Hydrology.hpp")
    #include "src/worldgen/Hydrology.hpp"
  #else
    // Fallback if your include dirs don't contain the repo root.
    #include "../../src/worldgen/Hydrology.hpp"
  #endif
#else
  #include "../../src/worldgen/Hydrology.hpp"
#endif
