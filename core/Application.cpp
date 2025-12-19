// core/Application.cpp
#include "core/Application.hpp"

#include "core/Game.hpp"
#include "core/Window.hpp" // wraps HWND + message pump

#include <chrono>
#include <exception>
#include <thread>

namespace
{
    using Clock = std::chrono::steady_clock;

    // These "feature probes" keep this file compatible with both styles:
    //   Game::tick() and Game::tick(dt)
    //   Game::render() and Game::render(alpha)
    template <class T>
    constexpr bool HasTickDouble = requires(T& t, double dt) { t.tick(dt); };

    template <class T>
    constexpr bool HasTickFloat = requires(T& t, float dt) { t.tick(dt); };

    template <class T>
    constexpr bool HasRenderAlphaDouble = requires(T& t, double a) { t.render(a); };

    template <class T>
    constexpr bool HasRenderAlphaFloat = requires(T& t, float a) { t.render(a); };

    template <class T>
    constexpr bool HasRenderNoArgs = requires(T& t) { t.render(); };
}

int RunColonyGame(HINSTANCE hInstance)
{
    try
    {
        Window window{hInstance, L"Colony Game", 1600, 900};
        Game   game{};

        // Fixed timestep simulation (60Hz). Rendering can be variable-rate.
        constexpr double kFixedDtSeconds = 1.0 / 60.0;
        constexpr double kMaxFrameSeconds = 0.25; // clamp to prevent spiral-of-death after stalls

        double accumulator = 0.0;
        auto   last = Clock::now();

        while (!window.shouldClose())
        {
            window.pollMessages();

            const auto now = Clock::now();
            std::chrono::duration<double> frame = now - last;
            last = now;

            double dt = frame.count();
            if (dt < 0.0) dt = 0.0;
            if (dt > kMaxFrameSeconds) dt = kMaxFrameSeconds;

            accumulator += dt;

            // Step the simulation in fixed increments.
            while (accumulator >= kFixedDtSeconds)
            {
                if constexpr (HasTickDouble<Game>)
                {
                    game.tick(kFixedDtSeconds);
                }
                else if constexpr (HasTickFloat<Game>)
                {
                    game.tick(static_cast<float>(kFixedDtSeconds));
                }
                else
                {
                    game.tick();
                }

                accumulator -= kFixedDtSeconds;
            }

            // Optional interpolation alpha for smooth rendering if you support it later.
            const double alpha = (kFixedDtSeconds > 0.0) ? (accumulator / kFixedDtSeconds) : 0.0;

            if constexpr (HasRenderAlphaDouble<Game>)
            {
                game.render(alpha);
            }
            else if constexpr (HasRenderAlphaFloat<Game>)
            {
                game.render(static_cast<float>(alpha));
            }
            else if constexpr (HasRenderNoArgs<Game>)
            {
                game.render();
            }

            // If vsync is off, this prevents pegging a CPU core at 100%.
            // (If your renderer already blocks on Present with vsync, this is basically free.)
            std::this_thread::yield();
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Colony Game - Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (...)
    {
        MessageBoxA(nullptr, "Unknown fatal error", "Colony Game - Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
