#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "core/Log.h"
#include <mutex>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <string>
#include <ctime>

namespace cg {
namespace {

    std::ofstream g_log;
    std::mutex    g_mu;

    std::string NowStamp() {
        using namespace std::chrono;
        auto t  = system_clock::to_time_t(system_clock::now());
        std::tm tm{};
        #if defined(_WIN32)
        localtime_s(&tm, &t);
        #else
        localtime_r(&t, &tm);
        #endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::filesystem::path DefaultLogDir() {
        // Use the system temp dir: %TEMP%\ColonyGame\Logs (Windows)
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path(ec);
        if (ec) {
            base = std::filesystem::path(".");
        }
        return base / "ColonyGame" / "Logs";
    }

    bool OpenLogAt(const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        auto path = dir / "ColonyGame.log";
        g_log.open(path, std::ios::out | std::ios::app | std::ios::binary);
        return g_log.good();
    }

    inline void MirrorToDebugger(const std::string& s) {
    #if defined(_WIN32)
        OutputDebugStringA(s.c_str());
    #endif
    }

} // namespace

void Log::Init(const std::filesystem::path& logDir) {
    std::scoped_lock lk(g_mu);

    // Try the caller-provided directory first; if it’s empty or fails, fall back to a safe default.
    bool opened = false;
    if (!logDir.empty()) {
        opened = OpenLogAt(logDir);
    }
    if (!opened) {
        opened = OpenLogAt(DefaultLogDir());
    }

    if (opened) {
        const std::string line = "[" + NowStamp() + "][INFO] === Log start ===\n";
        g_log.write(line.data(), static_cast<std::streamsize>(line.size()));
        g_log.flush(); // keep tail-followers (and crash logs) current
        MirrorToDebugger(line);
    } else {
        const std::string line = "[" + NowStamp() + "][WARN] Failed to open log file; debugger-only logging.\n";
        MirrorToDebugger(line);
    }
}

void Log::Shutdown() {
    std::scoped_lock lk(g_mu);
    if (g_log.good()) {
        const std::string line = "[" + NowStamp() + "][INFO] === Log end ===\n";
        g_log.write(line.data(), static_cast<std::streamsize>(line.size()));
        g_log.flush();
        MirrorToDebugger(line);
        g_log.close();
    }
}

void Log::Write(const char* level, const std::string& msg) {
    std::scoped_lock lk(g_mu);
    const std::string line = "[" + NowStamp() + "][" + level + "] " + msg + "\n";
    if (g_log.good()) {
        g_log.write(line.data(), static_cast<std::streamsize>(line.size()));
        g_log.flush();
    }
    MirrorToDebugger(line);
}

void Log::Info(const std::string& msg)  { Write("INFO",  msg); }
void Log::Warn(const std::string& msg)  { Write("WARN",  msg); }
void Log::Error(const std::string& msg) { Write("ERROR", msg); }

} // namespace cg

