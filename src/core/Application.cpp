// core/Application.cpp
//
// Minimal Windows-only application driver for Colony-Game.
// - No Linux/macOS codepaths.
// - Self-contained; does not depend on renderer or platform layers.
// - Uses std::chrono for timing (avoids extra project deps).
// - Includes pch.hpp if present.
// - If engine/Game.hpp exists, it is used; otherwise a tiny stub is compiled.
// - Exposes C-style hooks the Win32 bootstrap can call:
//
//     bool colony_app_init(HWND hwnd, int width, int height);
//     void colony_app_tick();            // run one frame (fixed-timestep inside)
//     void colony_app_resize(int w, int h);
//     void colony_app_shutdown();
//
// This file intentionally avoids Windows headers unless available; HWND is
// forward-declared to keep compile units lean and compatible with unity builds.

#if defined(_MSC_VER)
  // Keep MSVC in standards-conforming mode; no extensions required here.
  #pragma warning(push, 0)
#endif

#if defined(__has_include)
  #if __has_include("pch.hpp")
    #include "pch.hpp"
  #endif
#else
  // Older compilers: just proceed without pch.hpp
#endif

#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

// ---- Windows handle forward-declare (avoids <windows.h> requirement here) ----
struct HWND__;
using HWND = HWND__*;

// ---- Optional Game include ---------------------------------------------------
#ifndef __has_include
  #define __has_include(x) 0
#endif

#if __has_include("../engine/Game.hpp")
  #include "../engine/Game.hpp"
  #define COLONY_HAVE_GAME 1
#elif __has_include("Game.hpp")
  #include "Game.hpp"
  #define COLONY_HAVE_GAME 1
#else
  #define COLONY_HAVE_GAME 0
  // Fallback stub so this TU compiles even before Game is implemented.
  namespace colony {
    class Game {
    public:
      bool initialize(HWND /*hwnd*/, int /*w*/, int /*h*/) noexcept { return true; }
      void shutdown() noexcept {}
      void update(double /*dt*/) noexcept {}
      void render() noexcept {}
      void resize(int /*w*/, int /*h*/) noexcept {}
    };
  } // namespace colony
#endif

// ---- Application -------------------------------------------------------------
namespace colony {

class Application {
public:
  static Application& instance() noexcept {
    static Application s_app;
    return s_app;
  }

  // Initialize the game for the given window and client size.
  bool initialize(HWND hwnd, int width, int height) noexcept {
    m_hwnd   = hwnd;
    m_width  = width;
    m_height = height;

    m_game = std::make_unique<Game>();
    const bool ok = m_game->initialize(hwnd, width, height);
    m_running     = ok;
    m_accumulator = 0.0;
    m_prevTick    = std::chrono::steady_clock::now();
    return ok;
  }

  void shutdown() noexcept {
    if (m_game) {
      m_game->shutdown();
      m_game.reset();
    }
    m_running = false;
  }

  // Called when the platform layer reports a resize.
  void resize(int w, int h) noexcept {
    if (w <= 0 || h <= 0) return;
    m_width = w; m_height = h;
    if (m_game) m_game->resize(w, h);
  }

  // Ticks one frame: advances fixed-step sim, then renders.
  void tick() noexcept {
    if (!m_running) return;

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> dt = now - m_prevTick;
    m_prevTick = now;

    step_fixed(dt.count());
  }

  // Optional: change target FPS (e.g., 30/60/120).
  void set_target_fps(int fps) noexcept {
    if (fps > 0) m_targetDelta = 1.0 / static_cast<double>(fps);
  }

  bool running() const noexcept { return m_running; }

private:
  Application()  = default;
  ~Application() = default;
  Application(const Application&)            = delete;
  Application& operator=(const Application&) = delete;

  // Fixed-timestep accumulator loop (prevents spiral-of-death).
  void step_fixed(double dt) noexcept {
    // If the debugger hit a breakpoint or the app was de-focused,
    // clamp dt so we don't simulate a huge catch-up frame.
    if (dt > 0.25) dt = 0.25;

    m_accumulator += dt;

    // Run zero-or-more fixed updates to catch up to real time.
    while (m_accumulator >= m_targetDelta) {
      if (m_game) m_game->update(m_targetDelta);
      m_accumulator -= m_targetDelta;
    }

    // One render per tick (use latest game state).
    if (m_game) m_game->render();
  }

private:
  std::unique_ptr<Game> m_game;

  HWND  m_hwnd   = nullptr;
  int   m_width  = 0;
  int   m_height = 0;
  bool  m_running = false;

  // Timing
  double m_targetDelta = 1.0 / 60.0; // 60 Hz by default
  double m_accumulator = 0.0;
  std::chrono::steady_clock::time_point m_prevTick{};
};

} // namespace colony

// ---- C-style hooks for the Win32 entry layer --------------------------------
//
// These provide a stable ABI for platform/win/win_entry.cpp (or WinLauncher.cpp)
// to call without needing to know about the Application class. Linkers will
// happily pull these in from the core library.

extern "C" {

// Initialize the application with an existing HWND + initial client size.
bool colony_app_init(HWND hwnd, int width, int height) {
  return colony::Application::instance().initialize(hwnd, width, height);
}

// Tick one frame. Call this from your message loop when the window is active.
void colony_app_tick() {
  colony::Application::instance().tick();
}

// Notify the app that the client size has changed (e.g., on WM_SIZE).
void colony_app_resize(int width, int height) {
  colony::Application::instance().resize(width, height);
}

// Shutdown and free all resources.
void colony_app_shutdown() {
  colony::Application::instance().shutdown();
}

} // extern "C"

#if defined(_MSC_VER)
  #pragma warning(pop)
#endif
