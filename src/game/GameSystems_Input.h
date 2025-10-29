// File: src/game/GameSystems_Input.h
//
// Back‑compat shim for code that expects an "Input systems" header at this path.
// The canonical systems live in game/GameSystems.hpp in the current codebase.
//
// This header does two things:
//   1) If a dedicated input header exists, include it.
//   2) Otherwise, include the consolidated GameSystems.hpp,
//      and provide a minimal declaration (and inline stub) for
//      GameSystems::Input::Update(...) so callers like Game.cpp compile.
//
// NOTE: The actual input processing in this repo currently lives in
//       colony::ProcessInput(entt::registry&, const std::vector<InputEvent>&)
//       declared in game/GameSystems.hpp and implemented in
//       src/game/GameSystems_Input.cpp.
//
//       This shim is intentionally lightweight to avoid coupling;
//       the real work should occur in your Game tick using the colony::* APIs.

#pragma once

// Prefer a dedicated Input header if your project provides one later.
#if __has_include("game/GameSystems_Input.hpp")
  #include "game/GameSystems_Input.hpp"

#else
  // Fallback to the consolidated systems header used by the repo today.
  // (Declares colony::ProcessInput, colony::UpdateSimulation, colony::RenderFrame)
  #if __has_include("game/GameSystems.hpp")
    #include "game/GameSystems.hpp"
  #endif

  // Forward declare Camera to keep this header light-weight.
  // Game.cpp includes "ui/Camera.h" itself, so we don't need it here.
  struct Camera;

  namespace GameSystems {
    namespace Input {

      // Minimal API that Game.cpp expects to exist:
      //   GameSystems::Input::Update(camera, dtSeconds);
      //
      // We provide a harmless inline stub so you don't hit unresolved
      // externals while you keep migrating to the colony::* flow.
      // Hook your real input->camera wiring in your main Tick (via
      // colony::ProcessInput + your camera controller), not here.
      inline void Update(Camera& /*camera*/, double /*dtSeconds*/) noexcept {
        // Intentional no-op shim.
        // Real input processing currently lives in colony::ProcessInput(...)
        // (see game/GameSystems.hpp and src/game/GameSystems_Input.cpp).
      }

    } // namespace Input
  }   // namespace GameSystems
#endif
