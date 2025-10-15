#include "Game.hpp"
#include "GameSystems.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

// Async logging is optional; we include the header only when needed.
#include <spdlog/async.h>

#ifdef TRACY_ENABLE
  #include <tracy/Tracy.hpp>
#endif

namespace fs = std::filesystem;

namespace colony {

namespace {
std::shared_ptr<spdlog::logger> create_logger(bool async_logging) {
  try {
    fs::create_directories("logs");
  } catch (...) {
    // ignore; we'll try to log anyway
  }

  const auto log_path = fs::path("logs") / "colony.log";

  if (async_logging) {
    // Create a small thread-pool and async logger that drops oldest on overflow.
    spdlog::init_thread_pool(8192, 1);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);

    auto logger = std::make_shared<spdlog::async_logger>(
      "colony",
      sink,
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::overrun_oldest);

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    return logger;
  } else {
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
    auto logger = std::make_shared<spdlog::logger>("colony", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    return logger;
  }
}
} // namespace

Game::Game()  = default;
Game::~Game() = default;

void Game::Initialize(const GameConfig& cfg) {
  m_config  = cfg;
  m_logger  = create_logger(cfg.async_logging);
  m_running = true;

  // Task system
  m_executor = std::make_unique<tf::Executor>(); // default to HW concurrency
  m_taskflow = std::make_unique<tf::Taskflow>();

  // Registry bootstrap: reserve a little archetype memory up front if desired.
  // (No-op by default to avoid assumptions about your components.)

  spdlog::info("Colony Game initialized. async_logging={}, imgui_viewports={}",
               cfg.async_logging, cfg.enable_imgui_viewports);
}

void Game::Shutdown() {
  if (!m_running.exchange(false)) {
    return; // already shut down
  }

  // Ensure input queue is cleared and systems can finalize.
  {
    std::lock_guard<std::mutex> lock(m_inputMutex);
    m_inputQueue.clear();
  }

  // Destroy systems that might hold onto registry resources (none here).
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
  sink.clear();
  std::lock_guard<std::mutex> lock(m_inputMutex);
  if (!m_inputQueue.empty()) {
    sink.insert(sink.end(), m_inputQueue.begin(), m_inputQueue.end());
    m_inputQueue.clear();
  }
}

} // namespace colony
