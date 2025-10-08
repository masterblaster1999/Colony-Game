// src/game/core/Game.hpp
#pragma once
// #include "../../<your_path>/Game.hpp"
namespace colony::game::core {
  struct Game {
    // enough API for smoke tests (e.g., init/shutdown tick stubs)
    void init() noexcept {}
    void tick(float /*dt*/) noexcept {}
    void shutdown() noexcept {}
  };
}
