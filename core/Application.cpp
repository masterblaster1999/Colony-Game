// core/Application.cpp
#include "core/Application.hpp"

namespace core { class Window; } // allow referencing core::Window if Window.hpp uses that namespace
class Window;                    // allow referencing ::Window if Window.hpp is global

#include "core/Game.hpp"
#include "core/Window.hpp"

#include <chrono>
#include <exception>
#include <thread>
#include <type_traits>

namespace
{
    using Clock = std::chrono::steady_clock;

    template <class>
    struct always_false : std::false_type {};

    template <class T>
    concept CompleteType = requires { sizeof(T); };

    // Window might be in namespace core or global; support both without guessing.
    using WindowT = std::conditional_t<CompleteType<core::Window>, core::Window, ::Window>;
    using GameT   = core::Game;

    // -----------------------------
    // Compile-time “feature probes”
    // -----------------------------
    template <class T>
    constexpr bool HasTickDouble = requires(T& t, double dt) { t.tick(dt); };

    template <class T>
    constexpr bool HasTickFloat = requires(T& t, float dt) { t.tick(dt); };

    template <class T>
    constexpr bool HasTickNoArgs = requires(T& t) { t.tick(); };

    template <class T, class W>
    constexpr bool HasRenderWindowAlphaDouble = requires(T& t, W& w, double a) { t.render(w, a); };

    template <class T, class W>
    constexpr bool HasRenderWindowAlphaFloat = requires(T& t, W& w, float a) { t.render(w, a); };

    template <class T, class W>
    constexpr bool HasRenderWindowNoArgs = requires(T& t, W& w) { t.render(w); };

    template <class T>
    constexpr bool HasRenderAlphaDouble = requires(T& t, double a) { t.render(a); };

    template <class T>
    constexpr bool HasRenderAlphaFloat = requires(T& t, float a) { t.render(a); };

    template <class T>
    constexpr bool HasRenderNoArgs = requires(T& t) { t.render(); };

    // -----------------------------
    // Small Win32 helpers
    // -----------------------------
    void ShowFatalErrorA(const char* msg) noexcept
    {
        ::MessageBoxA(nullptr,
                      (msg && *msg) ? msg : "Unknown error",
                      "Colony Game - Fatal Error",
                      MB_OK | MB_ICONERROR);
    }

    void ShowFatalErrorW(const wchar_t* msg) noexcept
    {
        ::MessageBoxW(nullptr,
                      (msg && *msg) ? msg : L"Unknown error",
                      L"Colony Game - Fatal Error",
                      MB_OK | MB_ICONERROR);
    }

    // -----------------------------
    // Window API adapter helpers
    // -----------------------------
    template <class W>
    void PumpMessages(W& window)
    {
        if constexpr (requires { window.pollMessages(); })
        {
            window.pollMessages();
        }
        else if constexpr (requires { window.pumpMessages(); })
        {
            window.pumpMessages();
        }
        else if constexpr (requires { window.processMessages(); })
        {
            window.processMessages();
        }
        else
        {
            // If your Window wrapper uses a different name, add it above.
            static_assert(always_false<W>::value, "Window must expose a message pump (pollMessages/pumpMessages/processMessages).");
        }
    }

    template <class W>
    bool ShouldClose(W& window)
    {
        if constexpr (requires { window.shouldClose(); })
        {
            return static_cast<bool>(window.shouldClose());
        }
        else if constexpr (requires { window.isOpen(); })
        {
            return !static_cast<bool>(window.isOpen());
        }
        else
        {
            // If you don’t have an explicit close flag, return false.
            return false;
        }
    }

    template <class W>
    void PresentIfSupported(W& window)
    {
        if constexpr (requires { window.present(); })
        {
            window.present();
        }
        else if constexpr (requires { window.swapBuffers(); })
        {
            window.swapBuffers();
        }
        else if constexpr (requires { window.endFrame(); })
        {
            window.endFrame();
        }
        else
        {
            // No-op if presentation is handled elsewhere (e.g., renderer owns swapchain).
            (void)window;
        }
    }

    template <class W>
    W MakeWindow(HINSTANCE hInstance)
    {
        // Try common constructor/factory signatures.
        if constexpr (requires { W{ hInstance }; })
        {
            return W{ hInstance };
        }
        else if constexpr (requires { W{ hInstance, 1280, 720, L"Colony Game" }; })
        {
            return W{ hInstance, 1280, 720, L"Colony Game" };
        }
        else if constexpr (requires { W{ hInstance, L"Colony Game", 1280, 720 }; })
        {
            return W{ hInstance, L"Colony Game", 1280, 720 };
        }
        else if constexpr (requires { W::Create(hInstance); })
        {
            return W::Create(hInstance);
        }
        else if constexpr (requires { W w{}; } && requires(W& w) { w.create(hInstance); })
        {
            W w{};
            w.create(hInstance);
            return w;
        }
        else
        {
            static_assert(always_false<W>::value,
                          "No supported Window constructor found. Add your Window signature to MakeWindow().");
        }
    }

    template <class G, class W>
    G MakeGame(W& window)
    {
        // Prefer constructors that take the window, but fall back to default construction.
        if constexpr (requires { G{ window }; })
        {
            return G{ window };
        }
        else if constexpr (requires { G{ &window }; })
        {
            return G{ &window };
        }
        else if constexpr (requires { G{}; })
        {
            (void)window;
            return G{};
        }
        else
        {
            static_assert(always_false<G>::value, "No supported Game constructor found.");
        }
    }

    template <class G>
    void FixedTick(G& game, double dtSeconds)
    {
        if constexpr (HasTickDouble<G>)
        {
            game.tick(dtSeconds);
        }
        else if constexpr (HasTickFloat<G>)
        {
            game.tick(static_cast<float>(dtSeconds));
        }
        else if constexpr (HasTickNoArgs<G>)
        {
            (void)dtSeconds;
            game.tick();
        }
        else
        {
            static_assert(always_false<G>::value, "Game must implement tick(), tick(float), or tick(double).");
        }
    }

    template <class G, class W>
    void RenderFrame(G& game, W& window, double alpha)
    {
        if constexpr (HasRenderWindowAlphaDouble<G, W>)
        {
            game.render(window, alpha);
        }
        else if constexpr (HasRenderWindowAlphaFloat<G, W>)
        {
            game.render(window, static_cast<float>(alpha));
        }
        else if constexpr (HasRenderWindowNoArgs<G, W>)
        {
            (void)alpha;
            game.render(window);
        }
        else if constexpr (HasRenderAlphaDouble<G>)
        {
            game.render(alpha);
        }
        else if constexpr (HasRenderAlphaFloat<G>)
        {
            game.render(static_cast<float>(alpha));
        }
        else if constexpr (HasRenderNoArgs<G>)
        {
            (void)alpha;
            game.render();
        }
        else
        {
            static_assert(always_false<G>::value, "Game must implement render() overload(s).");
        }
    }
} // namespace

int RunColonyGame(HINSTANCE hInstance)
{
    try
    {
        WindowT window = MakeWindow<WindowT>(hInstance);

        GameT game = MakeGame<GameT>(window);

        // Fixed timestep (60Hz) with accumulator.
        constexpr double kFixedDtSeconds   = 1.0 / 60.0;
        constexpr double kMaxFrameSeconds  = 0.25; // avoid spiral-of-death after breakpoint / hitch

        auto last = Clock::now();
        double accumulator = 0.0;

        while (!ShouldClose(window))
        {
            PumpMessages(window);
            if (ShouldClose(window))
                break;

            const auto now = Clock::now();
            std::chrono::duration<double> frame = now - last;
            last = now;

            double dt = frame.count();
            if (dt < 0.0) dt = 0.0;
            if (dt > kMaxFrameSeconds) dt = kMaxFrameSeconds;

            accumulator += dt;

            while (accumulator >= kFixedDtSeconds)
            {
                FixedTick(game, kFixedDtSeconds);
                accumulator -= kFixedDtSeconds;
            }

            // Optional interpolation alpha for smoother rendering if supported.
            const double alpha = (kFixedDtSeconds > 0.0) ? (accumulator / kFixedDtSeconds) : 0.0;

            RenderFrame(game, window, alpha);

            // Only present if your Window wrapper exposes it; otherwise no-op.
            PresentIfSupported(window);

            // Friendly to CPU if present() doesn't block (optional).
            std::this_thread::yield();
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        ShowFatalErrorA(e.what());
    }
    catch (...)
    {
        ShowFatalErrorW(L"Unhandled unknown exception.");
    }

    return -1;
}
