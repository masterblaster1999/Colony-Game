// core/Application.cpp
//
// Windows-only application loop with fixed-timestep simulation.
// C++17-compatible.

#include "core/Application.hpp"
#include "core/Game.hpp"
#include "core/Window.hpp"

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <thread>
#include <type_traits>
#include <utility>
#include <string>
#include <stdexcept>

namespace core
{
namespace
{
using Clock = std::chrono::steady_clock;

template <typename...>
using void_t = void;

template <class T>
struct always_false : std::false_type {};

// -----------------------------
// Win32 helpers
// -----------------------------

inline void ShowFatalErrorW(const wchar_t* msg) noexcept
{
    ::MessageBoxW(nullptr,
                  (msg && *msg) ? msg : L"Unknown error",
                  L"Colony Game - Fatal Error",
                  MB_OK | MB_ICONERROR);
}

inline void SetCurrentThreadDescriptionBestEffort(const wchar_t* name) noexcept
{
    using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

    HMODULE kernel = ::GetModuleHandleW(L"Kernel32.dll");
    if (!kernel)
        return;

    auto fn = reinterpret_cast<Fn>(::GetProcAddress(kernel, "SetThreadDescription"));
    if (fn)
        (void)fn(::GetCurrentThread(), name);
}

// Thread-wide message pump. Returns false if WM_QUIT received.
inline bool PumpWin32Messages(int& outExitCode) noexcept
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            outExitCode = static_cast<int>(msg.wParam);
            return false;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

// -----------------------------
// Detection idiom (C++17) for Game API
// -----------------------------

template <class T, class = void>
struct has_tick_double : std::false_type {};
template <class T>
struct has_tick_double<T, void_t<decltype(std::declval<T&>().tick(std::declval<double>()))>> : std::true_type {};

template <class T, class = void>
struct has_tick_float : std::false_type {};
template <class T>
struct has_tick_float<T, void_t<decltype(std::declval<T&>().tick(std::declval<float>()))>> : std::true_type {};

template <class T, class = void>
struct has_tick_noargs : std::false_type {};
template <class T>
struct has_tick_noargs<T, void_t<decltype(std::declval<T&>().tick())>> : std::true_type {};

template <class T, class W, class = void>
struct has_render_window_alpha_float : std::false_type {};
template <class T, class W>
struct has_render_window_alpha_float<T, W,
    void_t<decltype(std::declval<T&>().render(std::declval<W&>(), std::declval<float>()))>> : std::true_type {};

template <class T, class W, class = void>
struct has_render_window_alpha_double : std::false_type {};
template <class T, class W>
struct has_render_window_alpha_double<T, W,
    void_t<decltype(std::declval<T&>().render(std::declval<W&>(), std::declval<double>()))>> : std::true_type {};

template <class T, class W, class = void>
struct has_render_window_noargs : std::false_type {};
template <class T, class W>
struct has_render_window_noargs<T, W,
    void_t<decltype(std::declval<T&>().render(std::declval<W&>()))>> : std::true_type {};

template <class T, class = void>
struct has_render_alpha_double : std::false_type {};
template <class T>
struct has_render_alpha_double<T, void_t<decltype(std::declval<T&>().render(std::declval<double>()))>> : std::true_type {};

template <class T, class = void>
struct has_render_noargs : std::false_type {};
template <class T>
struct has_render_noargs<T, void_t<decltype(std::declval<T&>().render())>> : std::true_type {};

template <class GameT>
void TickGame(GameT& game, double dtSeconds)
{
    if constexpr (has_tick_double<GameT>::value)
    {
        game.tick(dtSeconds);
    }
    else if constexpr (has_tick_float<GameT>::value)
    {
        game.tick(static_cast<float>(dtSeconds));
    }
    else if constexpr (has_tick_noargs<GameT>::value)
    {
        (void)dtSeconds;
        game.tick();
    }
    else
    {
        static_assert(always_false<GameT>::value, "Game must provide tick(), tick(float) or tick(double).");
    }
}

template <class GameT, class WindowT>
void RenderGame(GameT& game, WindowT& window, double alpha)
{
    if constexpr (has_render_window_alpha_float<GameT, WindowT>::value)
    {
        game.render(window, static_cast<float>(alpha));
    }
    else if constexpr (has_render_window_alpha_double<GameT, WindowT>::value)
    {
        game.render(window, alpha);
    }
    else if constexpr (has_render_window_noargs<GameT, WindowT>::value)
    {
        (void)alpha;
        game.render(window);
    }
    else if constexpr (has_render_alpha_double<GameT>::value)
    {
        game.render(alpha);
    }
    else if constexpr (has_render_noargs<GameT>::value)
    {
        (void)alpha;
        game.render();
    }
    else
    {
        static_assert(always_false<GameT>::value,
                      "Game must provide render(), render(alpha), render(window), or render(window, alpha).");
    }
}

// -----------------------------
// Detection idiom (C++17) for Window API
// -----------------------------

template <class W, class = void>
struct has_should_close : std::false_type {};
template <class W>
struct has_should_close<W, void_t<decltype(std::declval<W&>().shouldClose())>> : std::true_type {};

template <class W, class = void>
struct has_is_open : std::false_type {};
template <class W>
struct has_is_open<W, void_t<decltype(std::declval<W&>().isOpen())>> : std::true_type {};

template <class W, class = void>
struct has_present : std::false_type {};
template <class W>
struct has_present<W, void_t<decltype(std::declval<W&>().present())>> : std::true_type {};

template <class W, class = void>
struct has_swap_buffers : std::false_type {};
template <class W>
struct has_swap_buffers<W, void_t<decltype(std::declval<W&>().swapBuffers())>> : std::true_type {};

template <class W, class = void>
struct has_end_frame : std::false_type {};
template <class W>
struct has_end_frame<W, void_t<decltype(std::declval<W&>().endFrame())>> : std::true_type {};

template <class W, class = void>
struct has_show_cmd : std::false_type {};
template <class W>
struct has_show_cmd<W, void_t<decltype(std::declval<W&>().show(std::declval<int>()))>> : std::true_type {};

template <class W, class = void>
struct has_show_noargs : std::false_type {};
template <class W>
struct has_show_noargs<W, void_t<decltype(std::declval<W&>().show())>> : std::true_type {};

template <class WindowT>
bool ShouldClose(WindowT& window)
{
    if constexpr (has_should_close<WindowT>::value)
        return static_cast<bool>(window.shouldClose());

    if constexpr (has_is_open<WindowT>::value)
        return !static_cast<bool>(window.isOpen());

    return false;
}

template <class WindowT>
void PresentIfSupported(WindowT& window)
{
    if constexpr (has_present<WindowT>::value)
        window.present();
    else if constexpr (has_swap_buffers<WindowT>::value)
        window.swapBuffers();
    else if constexpr (has_end_frame<WindowT>::value)
        window.endFrame();
    else
        (void)window;
}

template <class WindowT>
void ShowIfSupported(WindowT& window, int nCmdShow)
{
    if constexpr (has_show_cmd<WindowT>::value)
        window.show(nCmdShow);
    else if constexpr (has_show_noargs<WindowT>::value)
        window.show();
    else
        (void)window, (void)nCmdShow;
}

inline std::wstring Utf8ToWideBestEffort(const char* s)
{
    if (!s || !*s)
        return L"Unknown error";

    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0)
        return L"Unknown error";

    std::wstring out;
    out.resize(static_cast<size_t>(len - 1));
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], len);
    return out;
}

} // anonymous namespace

int RunApplication(HINSTANCE hInstance, int nCmdShow, ApplicationDesc desc)
{
    // SINGLE EXIT POINT (prevents C4702 “unreachable code” tails under /WX)
    int finalExitCode = -1;

    try
    {
        SetCurrentThreadDescriptionBestEffort(L"ColonyGame Main Thread");

        core::Window window(hInstance, desc.width, desc.height, desc.title);
        ShowIfSupported(window, nCmdShow);

        core::Game game{};

        const double fixedDt  = (desc.fixed_dt_seconds > 0.0) ? desc.fixed_dt_seconds : (1.0 / 60.0);
        const double maxFrame = (desc.max_frame_time_seconds > 0.0) ? desc.max_frame_time_seconds : 0.25;

        auto prev = Clock::now();
        double accumulator = 0.0;

        int exitCode = 0;

        while (PumpWin32Messages(exitCode))
        {
            if (ShouldClose(window))
                break;

            const auto now = Clock::now();
            double frameTime = std::chrono::duration<double>(now - prev).count();
            prev = now;

            if (frameTime < 0.0) frameTime = 0.0;
            if (frameTime > maxFrame) frameTime = maxFrame;

            accumulator += frameTime;

            while (accumulator >= fixedDt)
            {
                TickGame(game, fixedDt);
                accumulator -= fixedDt;
            }

            const double alpha = (fixedDt > 0.0) ? (accumulator / fixedDt) : 0.0;

            RenderGame(game, window, alpha);
            PresentIfSupported(window);

            if (!desc.vsync)
                std::this_thread::yield();
        }

        finalExitCode = exitCode;
    }
    catch (const std::exception& e)
    {
        const std::wstring w = Utf8ToWideBestEffort(e.what());
        ShowFatalErrorW(w.c_str());
        finalExitCode = -1;
    }
    catch (...)
    {
        ShowFatalErrorW(L"Unknown exception");
        finalExitCode = -1;
    }

    return finalExitCode;
}

} // namespace core
