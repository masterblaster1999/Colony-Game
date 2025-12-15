#pragma once
#include "core/Log.h"

namespace cg {
struct Log {
    static void Info(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        core::LogMessageV(core::LogLevel::Info, fmt, args);
        va_end(args);
    }
    static void Warn(const char* fmt, ...) { /* same, LogLevel::Warn */ }
    static void Error(const char* fmt, ...) { /* same, LogLevel::Error */ }
};
}
