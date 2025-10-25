#include "Log.h"
#include <windows.h>
#include <cstdio>
#include <mutex>
#include <cstdarg>
#include <string>
#include <filesystem>

namespace core {
static std::mutex g_mutex;
static FILE* g_file = nullptr;

static const char* LevelTag(LogLevel lvl) {
    switch (lvl) {
    case LogLevel::Trace:    return "TRACE";
    case LogLevel::Info:     return "INFO ";
    case LogLevel::Warn:     return "WARN ";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "FATAL";
    }
    return "UNKWN";
}

void LogInit(const std::filesystem::path& logDir) {
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    const auto file = logDir / "game.log";
    _wfopen_s(&g_file, file.wstring().c_str(), L"w, ccs=UTF-8");

    // Convert filesystem path to a narrow string for the narrow logger.
    const std::string fileN = file.string();
    LOG_INFO("Logger initialized at %s", fileN.c_str());
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fflush(g_file);
        fclose(g_file);
        g_file = nullptr;
    }
}

void LogMessage(LogLevel level, const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);

    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    char line[2100];
    snprintf(line, sizeof(line), "[%s] %s\n", LevelTag(level), buf);

    // OutputDebugStringA for debugger visibility
    OutputDebugStringA(line);

    if (g_file) {
        fputs(line, g_file);
        fflush(g_file);
    }
}

} // namespace core
