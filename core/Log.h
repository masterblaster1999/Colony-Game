#pragma once
#include <cstdarg>

namespace core {

enum class LogLevel { Trace, Info, Warn, Error };

void LogMessageV(LogLevel level, const char* fmt, va_list args);
void LogMessage(LogLevel level, const char* fmt, ...);

} // namespace core
