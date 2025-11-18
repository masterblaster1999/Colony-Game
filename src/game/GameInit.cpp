#include "Game.hpp"
#include "GameSystems.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <mutex>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
// Async logging is optional; we include the header only when needed.
#include <spdlog/async.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
#endif

// Taskflow: header-only task system
#include <taskflow/taskflow.hpp>

namespace fs = std::filesystem;

namespace colony {

namespace {

// Common default logger configuration.
void configure_default_logger(const std::shared_ptr<spdlog::logger>& logger) {
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::info);
  spdlog::flush_on(spdlog::level::warn);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
}

std::shared_ptr<spdlog::logger> create_logger(bool async_logging) {
  try {
    fs::create_directories("logs");
  } catch (...) {
    // ignore; we'll try to log anyway
  }

  const auto log_path = fs::path("logs") / "colony.log";

  std::shared_ptr<spdlog::logger> logger;

  if (async_logging) {
    // Create a small thread-pool and async logger that drops oldest on overflow.
    // Guard against multiple initializations in case this is ever called more than once.
    static std::once_flag s_thread_pool_once;
    std::call_once(s_thread_pool_once, [] {
      spdlog::init_thread_pool(8192, 1);
    });

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);

    logger = std::make_shared<spdlog::async_logger>(
      "colony",
      std::move(sink),
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::overrun_oldest);
  } else {
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
    logger = std::make_shared<spdlog::logger>("colony", std::move(sink));
  }

  configure_default_logger(logger);
  return logger;
}

} // namespace

Game::Game()  = default;
Game::~Game() = default;

void Game::Initialize(const GameConfig& cfg) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Game::Initialize");
#endif

  m_config  = cfg;
  m_logger  = create_logger(cfg.async_logging);
  m_running = true;

  // Task system: default to hardware concurrency.
  m_executor = std::make_unique<tf::Executor>();
  m_taskflow = std::make_unique<tf::Taskflow>();

  // Registry bootstrap: reserve a little archetype memory up front if desired.
  // (No-op by default to avoid assumptions about your components.)

  spdlog::info("Colony Game initialized. async_logging={}, imgui_viewports={}",
               cfg.async_logging, cfg.enable_imgui_viewports);
}

void Game::Shutdown() {
#ifdef TRACY_ENABLE
  ZoneScopedN("Game::Shutdown");
#endif

  if (!m_running.exchange(false)) {
    return; // already shut down
  }

  // Ensure input queue is cleared and systems can finalize.
  {
    std::lock_guard<std::mutex> lock(m_inputMutex);
    m_inputQueue.clear();
  }

  // Destroy systems that might hold onto registry resources.
  m_taskflow.reset();
  m_executor.reset();

  spdlog::info("Colony Game shutdown.");
}

void Game::RequestQuit() {
  m_running = false;
}

void Game::PushInput(const InputEvent& e) {
  std::lock_guard<std::mutex> lock(m_inputMutex);
  m_inputQueue.emplace_back(e);
}

void Game::processInputQueue(std::vector<InputEvent>& sink) {
  std::lock_guard<std::mutex> lock(m_inputMutex);

  // Move accumulated input events out in O(1) by swapping.
  sink.clear();
  sink.swap(m_inputQueue);
}

} // namespace colony
