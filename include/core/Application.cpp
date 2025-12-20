// core/Application.cpp
// Windows-only baseline application loop with fixed-timestep simulation.

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <Windows.h>

#include <chrono>
#include <thread>
#include <stdexcept>
#include <utility>

// IMPORTANT:
// These includes assume your CMake adds ${PROJECT_SOURCE_DIR}/include to include dirs.
// (see target_include_directories fix)
#include <core/Application.hpp>
#include <core/Game.hpp>

namespace colony::core
{
namespace
{
  // Best-effort thread naming in the debugger (Win10+). Safe no-op if unavailable.
  void SetCurrentThreadDescription(const wchar_t* name) noexcept
  {
    using SetThreadDescriptionFn = HRESULT (WINAPI*)(HANDLE, PCWSTR);

    HMODULE kernel = ::GetModuleHandleW(L"Kernel32.dll");
    if (!kernel)
      return;

    auto fn = reinterpret_cast<SetThreadDescriptionFn>(
      ::GetProcAddress(kernel, "SetThreadDescription")
    );

    if (fn)
      (void)fn(::GetCurrentThread(), name);
  }

  // Win32 message pump. Returns false if WM_QUIT received.
  bool PumpWin32Messages(int& outExitCode) noexcept
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
} // namespace

// --------------------------
// If your header differs, adjust these to match your declared interface.
// --------------------------

Application::Application(ApplicationConfig config)
  : m_config(std::move(config))
{
}

Application::~Application() = default;

int Application::Run(std::unique_ptr<Game> game)
{
  if (!game)
    throw std::invalid_argument("Application::Run: game must not be null");

  SetCurrentThreadDescription(L"Colony Main Thread");

  m_game = std::move(game);
  m_exitCode = 0;
  m_quitRequested = false;

  // Game init
  m_game->OnInit(*this);

  using clock = std::chrono::steady_clock;

  auto lastTime = clock::now();
  auto accumulator = std::chrono::duration<double>(0.0);

  const double fixedDtSec =
    (m_config.fixed_dt_seconds > 0.0) ? m_config.fixed_dt_seconds : (1.0 / 60.0);

  const auto fixedDt = std::chrono::duration<double>(fixedDtSec);

  // Optional: cap delta to avoid huge dt after breakpoints / window dragging
  const auto maxFrame = std::chrono::milliseconds(250);

  // Main loop
  while (!m_quitRequested)
  {
    // Win32 message pump
    if (!PumpWin32Messages(m_exitCode))
    {
      // WM_QUIT
      m_quitRequested = true;
      break;
    }

    // Timing
    const auto now = clock::now();
    auto frameDt = now - lastTime;
    lastTime = now;

    if (frameDt > maxFrame)
      frameDt = maxFrame;

    accumulator += frameDt;

    // Fixed-step updates
    while (accumulator >= fixedDt)
    {
      m_game->OnFixedUpdate(*this, fixedDt.count());
      accumulator -= fixedDt;
    }

    const double alpha = (fixedDt.count() > 0.0) ? (accumulator / fixedDt) : 0.0;

    // Variable-step update + render
    m_game->OnUpdate(*this, std::chrono::duration<double>(frameDt).count(), alpha);
    m_game->OnRender(*this, alpha);

    // Optional FPS cap (0 or less means uncapped)
    if (m_config.target_fps > 0)
    {
      const auto targetFrame = std::chrono::duration<double>(1.0 / double(m_config.target_fps));
      const auto workTime = clock::now() - now;

      if (workTime < targetFrame)
      {
        // Coarse sleep first, then yield-spin to tighten.
        auto remaining = targetFrame - workTime;
        if (remaining > std::chrono::milliseconds(2))
          std::this_thread::sleep_for(remaining - std::chrono::milliseconds(1));

        while ((clock::now() - now) < targetFrame)
          std::this_thread::yield();
      }
    }
  }

  // Shutdown
  if (m_game)
  {
    m_game->OnShutdown(*this);
    m_game.reset();
  }

  return m_exitCode;
}

void Application::RequestQuit(int exitCode) noexcept
{
  m_exitCode = exitCode;
  m_quitRequested = true;
}

} // namespace colony::core
