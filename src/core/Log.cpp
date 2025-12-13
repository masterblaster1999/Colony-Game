#include "Log.h"

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>

namespace core {

static std::mutex g_mutex;
static FILE* g_file = nullptr;

static const char* LevelTag(LogLevel lvl)
{
    switch (lvl)
    {
    case LogLevel::Trace:    return "TRACE";
    case LogLevel::Info:     return "INFO ";
    case LogLevel::Warn:     return "WARN ";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "FATAL";
    }
    return "UNKWN";
}

static std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty())
        return {};

    if (ws.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return {};

    const int wlen = static_cast<int>(ws.size());

    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), wlen,
        nullptr, 0,
        nullptr, nullptr);

    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');

    const int written = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), wlen,
        out.data(), needed,
        nullptr, nullptr);

    if (written != needed)
        out.clear();

    return out;
}

void LogInit(const std::filesystem::path& logDir)
{
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    const auto file = logDir / L"game.log";

    // Open as binary and write UTF-8 bytes ourselves (robust, avoids CRT wide/narrow "ccs" pitfalls).
    FILE* newFile = nullptr;
    _wfopen_s(&newFile, file.c_str(), L"wb");

    if (newFile)
    {
        // Optional UTF-8 BOM (helps some Windows tools; harmless for most parsers)
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        (void)std::fwrite(bom, 1, sizeof(bom), newFile);
        (void)std::fflush(newFile);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        // Close previous file if LogInit is called more than once.
        if (g_file)
        {
            std::fflush(g_file);
            std::fclose(g_file);
            g_file = nullptr;
        }

        g_file = newFile;
    }

    // Log the initialized path (UTF-8), without relying on macros (avoids any init-order surprises).
    const std::string fileUtf8 = WideToUtf8(file.wstring());
    LogMessage(LogLevel::Info, "Logger initialized at %s", fileUtf8.empty() ? "<unavailable>" : fileUtf8.c_str());
}

void LogShutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file)
    {
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = nullptr;
    }
}

// IMPORTANT PATCH:
// Provide the missing symbol referenced by Bootstrap.obj:
//   core::LogMessageV(LogLevel, const char*, va_list)
// On MSVC, va_list is typically a char* typedef, which matches the linker signature you saw.
void LogMessageV(LogLevel level, const char* fmt, va_list ap)
{
    if (!fmt)
        return;

    char msg[2048]{};
#if defined(_MSC_VER)
    (void)vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
#else
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
#endif

    char line[2100]{};
 #if defined(_MSC_VER)
    (void)_snprintf_s(line, sizeof(line), _TRUNCATE, "[%s] %s\n", LevelTag(level), msg);
#else
    (void)snprintf(line, sizeof(line), "[%s] %s\n", LevelTag(level), msg);
#endif

    std::lock_guard<std::mutex> lock(g_mutex);

    // Debugger visibility
    ::OutputDebugStringA(line);

    // File output (UTF-8 bytes)
    if (g_file)
    {
        (void)std::fwrite(line, 1, std::strlen(line), g_file);
        (void)std::fflush(g_file);
    }
}

void LogMessage(LogLevel level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogMessageV(level, fmt, ap);
    va_end(ap);
}

} // namespace core
