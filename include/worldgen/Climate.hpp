#pragma once

#if __has_include("../../src/worldgen/Climate.hpp")
  #include "../../src/worldgen/Climate.hpp"
#elif __has_include("Climate.hpp")
  // Fallback for builds that put src/worldgen on the include path directly.
  #include "Climate.hpp"
#else
  #error "Could not find ../../src/worldgen/Climate.hpp"
#endif
