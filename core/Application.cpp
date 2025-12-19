// core/Application.cpp

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <exception>

#include "core/Application.hpp"
#include "core/Game.hpp"
#include "core/Window.hpp" // wraps HWND + message pump

namespace
{
    // Prefer a DPI-aware process so the window isn't blurry on high-DPI monitors.
    // Microsoft recommends setting DPI awareness via manifest when possible; this is a safe runtime fallback.
    void EnableDpiAwareness() noexcept
    {
#if defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        (void)::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#elif defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)
        (void)::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
#else
        (void)::SetProcessDPIAware();
#endif
    }

    // Compile-time “adapter” so Application.cpp keeps working if you later change Game's API:
    // - tick() OR tick(float) OR tick(double)
    template <class GameT>
    void TickGame(GameT& game, double dtSeconds)
    {
        if constexpr (requires(GameT& g, double dt) { g.tick(dt); })
        {
            game.tick(dtSeconds);
        }
        else if constexpr (requires(GameT& g, float dt) { g.tick(dt); })
        {
            game.tick(static_cast<float>(dtSeconds));
        }
        else if constexpr (requires(GameT& g) { g.tick(); })
        {
            (void)dtSeconds;
            game.tick();
        }
        else
        {
            static_assert(requires(GameT& g) { g.tick(); },
                          "Game must implement tick() (optionally with dt).");
        }
    }

    // Same idea for render(): render() OR render(Window&)
    template <class GameT, class WindowT>
    void RenderGame(GameT& game, WindowT& window)
    {
        if constexpr (requires(GameT& g, WindowT& w) { g.render(w); })
        {
            game.render(window);
        }
        else if constexpr (requires(GameT& g) { g.render(); })
        {
            (void)window;
            game.render();
        }
        else
        {
            static_assert(requires(GameT& g) { g.render(); },
                          "Game must implement render() (optionally taking Window&).");
        }
    }

    void ShowFatalErrorA(const char* msg) noexcept
    {
        ::MessageBoxA(nullptr,
                      msg ? msg : "Unknown error",
                      "Colony Game - Fatal Error",
                      MB_OK | MB_ICONERROR);
    }

    void ShowFatalErrorW(const wchar_t* msg) noexcept
    {
        ::MessageBoxW(nullptr,
                      msg ? msg : L"Unknown error",
                      L"Colony Game - Fatal Error",
                      MB_OK | MB_ICONERROR);
    }
} // namespace

int RunColonyGame(HINSTANCE hInstance)
{
    EnableDpiAwareness();

    try
    {
        Window window{hInstance, L"Colony Game", 1600, 900};
        Game   game;

        using clock = std::chrono::steady_clock;
        auto last = clock::now();

        // Clamp huge delta times (breakpoints, window dragging, etc.) so simulation doesn't explode.
        // This is the “semi-fixed timestep” safety trick (even if your sim stays variable-step). :contentReference[oaicite:0]{index=0}
        constexpr double kMaxDeltaSeconds = 0.25;

        while (true)
        {
            window.pollMessages();
            if (window.shouldClose())
                break;

            const auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            if (dt < 0.0)
                dt = 0.0;
            if (dt > kMaxDeltaSeconds)
                dt = kMaxDeltaSeconds;

            TickGame(game, dt);        // update simulation, AI, jobs
            RenderGame(game, window);  // call renderer
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        ShowFatalErrorA(e.what());
        return 1;
    }
    catch (...)
    {
        ShowFatalErrorW(L"An unknown exception occurred.");
        return 2;
    }
}
