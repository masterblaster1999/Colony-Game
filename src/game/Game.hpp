#pragma once

// Minimal, stable includes in header.
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

#include <entt/entt.hpp>

// Forward decls to avoid heavy includes here.
namespace spdlog { class logger; }

namespace tf {
  class Executor;
  class Taskflow;
}

namespace colony {

struct GameConfig {
  bool enable_imgui_viewports = false;  // honored by your platform layer
  bool enable_gpu_validation  = false;  // honored by your renderer/device init
  bool async_logging          = true;   // spdlog async logger
};

enum class InputEventType : uint8_t {
  None = 0,
  Quit,
  KeyDown,
  KeyUp,
  MouseMove,
  MouseDown,
  MouseUp,
  MouseWheel
};

struct InputEvent {
  InputEventType type {InputEventType::None};
  uint32_t       a    {0};     // keycode or button or wheel delta
  int32_t        x    {0};     // mouse x (if applicable)
  int32_t        y    {0};     // mouse y (if applicable)
  bool           alt  {false};
  bool           ctrl {false};
  bool           shift{false};
};

struct GameTime {
  double   dt_seconds {0.0};
  double   time_since_start {0.0};
  uint64_t frame_index {0};
};

class Game {
public:
  Game();
  ~Game();

  // Lifecycle ---------------------------------------------------------------
  void Initialize(const GameConfig& cfg);
  void Shutdown();

  // Main loop entry points --------------------------------------------------
  // Tick = ProcessInput + UpdateSimulation + Render
  void Tick(double dt_seconds);

  // External input injection from your platform/launcher
  void PushInput(const InputEvent& e);

  // Quit handling
  void RequestQuit();
  bool ShouldQuit() const noexcept { return !m_running.load(std::memory_order_relaxed); }

  // Accessors ---------------------------------------------------------------
  const GameTime&   Time()     const noexcept { return m_time; }
  entt::registry&   Registry()       noexcept { return m_registry; }
  const GameConfig& Config()   const noexcept { return m_config; }

private:
  void processInputQueue(std::vector<InputEvent>& sink);

private:
  GameConfig                          m_config{};
  entt::registry                      m_registry{};

  // Taskflow (parallel jobs)
  std::unique_ptr<tf::Executor>       m_executor;
  std::unique_ptr<tf::Taskflow>       m_taskflow;

  // Input queue (single producer from platform, single consumer in Tick)
  std::mutex                          m_inputMutex;
  std::vector<InputEvent>             m_inputQueue;

  // Logging
  std::shared_ptr<spdlog::logger>     m_logger;

  // Time/loop
  GameTime                            m_time{};
  std::atomic_bool                    m_running{false};
};

} // namespace colony
