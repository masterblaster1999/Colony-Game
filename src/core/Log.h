#pragma once
#include <filesystem>
#include <string>

namespace core {

enum class LogLevel { Trace, Info, Warn, Error, Critical };

void LogInit(const std::filesystem::path& logDir);
void LogShutdown();

void LogMessage(LogLevel level, const char* fmt, ...);

// Convenience macros
#define LOG_TRACE(...)   ::core::LogMessage(::core::LogLevel::Trace,   __VA_ARGS__)
#define LOG_INFO(...)    ::core::LogMessage(::core::LogLevel::Info,    __VA_ARGS__)
#define LOG_WARN(...)    ::core::LogMessage(::core::LogLevel::Warn,    __VA_ARGS__)
#define LOG_ERROR(...)   ::core::LogMessage(::core::LogLevel::Error,   __VA_ARGS__)
#define LOG_CRITICAL(...)::core::LogMessage(::core::LogLevel::Critical,__VA_ARGS__)

} // namespace core
