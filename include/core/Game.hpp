// include/core/Game.hpp
#pragma once

#if !defined(_WIN32)
#error "Colony-Game targets Windows only."
#endif

namespace core
{
    // Minimal stub game object.
    // Expand this into your real game simulation + renderer.
    class Game
    {
    public:
        Game() = default;
        ~Game() = default;

        // Fixed-step update (dtSeconds in seconds).
        // Keep this noexcept so the outer loop can handle fatal errors consistently.
        void tick(double /*dtSeconds*/) noexcept
        {
            // TODO: simulation update
        }

        // Render with optional interpolation alpha [0..1].
        void render(double /*alpha*/ = 1.0) noexcept
        {
            // TODO: draw + present (or draw only, depending on your pipeline)
        }
    };
} // namespace core
