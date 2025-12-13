// src/core/Log.h
#pragma once

#include <cstdarg>
#include <filesystem>

#if defined(_MSC_VER)
    #include <sal.h>
    #define CORE_PRINTF_FMT _Printf_format_string_
#else
    #define CORE_PRINTF_FMT
#endif

namespace core
{
    enum class LogLevel
    {
        Trace,
        Info,
        Warn,
        Error,
        Critical
    };

    // Initialize/shutdown logging; caller chooses log directory (Windows: %LOCALAPPDATA%\ColonyGame\logs, etc.)
    void LogInit(const std::filesystem::path& logDir);
    void LogShutdown();

    // Printf-style logging entry point.
    void LogMessage(LogLevel level, CORE_PRINTF_FMT const char* fmt, ...);

    // va_list variant for wrappers/forwarding.
    void LogMessageV(LogLevel level, CORE_PRINTF_FMT const char* fmt, va_list args);
} // namespace core

// Convenience macros (guarded to avoid accidental redefinition)
#ifndef LOG_TRACE
    #define LOG_TRACE(...) ::core::LogMessage(::core::LogLevel::Trace, __VA_ARGS__)
#endif
#ifndef LOG_INFO
    #define LOG_INFO(...) ::core::LogMessage(::core::LogLevel::Info, __VA_ARGS__)
#endif
#ifndef LOG_WARN
    #define LOG_WARN(...) ::core::LogMessage(::core::LogLevel::Warn, __VA_ARGS__)
#endif
#ifndef LOG_ERROR
    #define LOG_ERROR(...) ::core::LogMessage(::core::LogLevel::Error, __VA_ARGS__)
#endif
#ifndef LOG_CRITICAL
    #define LOG_CRITICAL(...) ::core::LogMessage(::core::LogLevel::Critical, __VA_ARGS__)
#endif

// -----------------------------------------------------------------------------
// Backward/sideways compatibility shims
// These adapters allow existing code that uses either `cg::log::info(...)`
// or `cg::Log::Info(...)` to compile without touching call sites.
// -----------------------------------------------------------------------------
namespace cg
{
    namespace log
    {
        inline void trace(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Trace, fmt, args);
            va_end(args);
        }

        inline void info(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Info, fmt, args);
            va_end(args);
        }

        inline void warn(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Warn, fmt, args);
            va_end(args);
        }

        inline void error(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Error, fmt, args);
            va_end(args);
        }

        inline void critical(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Critical, fmt, args);
            va_end(args);
        }
    } // namespace log

    struct Log
    {
        static void Trace(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Trace, fmt, args);
            va_end(args);
        }

        static void Info(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Info, fmt, args);
            va_end(args);
        }

        static void Warn(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Warn, fmt, args);
            va_end(args);
        }

        static void Error(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Error, fmt, args);
            va_end(args);
        }

        static void Critical(CORE_PRINTF_FMT const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            ::core::LogMessageV(::core::LogLevel::Critical, fmt, args);
            va_end(args);
        }
    };
} // namespace cg

#undef CORE_PRINTF_FMT
