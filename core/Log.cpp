#include "core/Log.h"
#include <cstdio>
#include <mutex>
#include <windows.h>

namespace core {

static std::mutex g_logMutex;

static const char* LevelPrefix(LogLevel lvl)
{
    switch (lvl)
    {
    case LogLevel::Trace: return "[Trace] ";
    case LogLevel::Info:  return "[Info ] ";
    case LogLevel::Warn:  return "[Warn ] ";
    case LogLevel::Error: return "[Error] ";
    default:              return "[Log  ] ";
    }
}

void LogMessageV(LogLevel level, const char* fmt, va_list args)
{
    if (!fmt) return;

    char msg[4096];

#if defined(_MSC_VER)
    int n = vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
#else
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
#endif
    if (n < 0) msg[0] = '\0';

    std::lock_guard<std::mutex> lock(g_logMutex);

    // OutputDebugString is great for Visual Studio + DebugView
    OutputDebugStringA(LevelPrefix(level));
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    // Optional: also print to console if attached
    DWORD mode = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode))
    {
        DWORD written = 0;
        WriteConsoleA(hOut, LevelPrefix(level), (DWORD)strlen(LevelPrefix(level)), &written, nullptr);
        WriteConsoleA(hOut, msg, (DWORD)strlen(msg), &written, nullptr);
        WriteConsoleA(hOut, "\n", 1, &written, nullptr);
    }
}

void LogMessage(LogLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogMessageV(level, fmt, args);
    va_end(args);
}

} // namespace core
