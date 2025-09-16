#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include "core/Log.h"
#include <mutex>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace cg {
namespace {
    std::ofstream g_log;
    std::mutex    g_mu;

    std::string NowStamp() {
        using namespace std::chrono;
        auto t  = system_clock::to_time_t(system_clock::now());
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
}

void Log::Init(const std::filesystem::path& logDir) {
    std::scoped_lock lk(g_mu);
    std::filesystem::create_directories(logDir);
    auto path = logDir / "ColonyGame.log";
    g_log.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (g_log.good()) {
        Write("INFO", "=== Log start ===");
    }
}

void Log::Shutdown() {
    std::scoped_lock lk(g_mu);
    if (g_log.good()) {
        Write("INFO", "=== Log end ===");
        g_log.flush();
        g_log.close();
    }
}

void Log::Write(const char* level, const std::string& msg) {
    std::scoped_lock lk(g_mu);
    std::string line = "[" + NowStamp() + "][" + level + "] " + msg + "\n";
    if (g_log.good()) g_log.write(line.data(), (std::streamsize)line.size());
    // Mirror to debugger
    OutputDebugStringA(line.c_str());
}

void Log::Info(const std::string& msg) { Write("INFO", msg); }
void Log::Warn(const std::string& msg) { Write("WARN", msg); }
void Log::Error(const std::string& msg) { Write("ERROR", msg); }

} // namespace cg
