#pragma once

// Compatibility shim for tests including "game/core/Game.h"
// Patched to prefer the real path at: ../../core/Game.hpp

#if __has_include("../../core/Game.hpp")
  #include "../../core/Game.hpp"
#elif __has_include("../../core/Game.h")
  #include "../../core/Game.h"
#elif __has_include("core/Game.hpp")
  #include "core/Game.hpp"
#elif __has_include("core/Game.h")
  #include "core/Game.h"
#elif __has_include("../../engine/Game.hpp")
  #include "../../engine/Game.hpp"
#elif __has_include("../../engine/Game.h")
  #include "../../engine/Game.h"
#else
  #error "Game core header not found. Update this shim with the actual path."
#endif
