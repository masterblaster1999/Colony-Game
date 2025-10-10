#pragma once

// Game shim → include the real Game class if available, else a stub that satisfies tests.

#if __has_include("game/Game.h")
  #include "game/Game.h"
#elif __has_include("engine/Game.h")
  #include "engine/Game.h"
#else
  struct Game {
    bool init() noexcept { return true; }
    void tick(float) noexcept {}
  };
#endif
