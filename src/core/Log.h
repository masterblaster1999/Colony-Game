#pragma once
#include <string>
#include <string_view>
#include <filesystem>

namespace cg {

/**
 * Tiny logger (Windows-first).
 *
 * Design goals (from PR 10):
 *  - Write to the Visual Studio Output window via OutputDebugString.
 *  - Also write to a log file under a caller-specified directory (e.g., %TEMP%/ColonyGame/Logs).
 *  - Simple, zero-dependency interface suitable for game loops.
 *
 * Implementation notes:
 *  - The file target and OutputDebugString integration are implemented in Log.cpp.
 *  - This header preserves your original API (Init(dir), Shutdown, Info/Warn/Error with std::string).
 *  - Extra inline overloads accept std::string_view/const char* and forward to the std::string API.
 */
class Log {
public:
    // Initialize the logger, creating the directory/file if needed.
    // Example: cg::Log::Init(std::filesystem::temp_directory_path() / "ColonyGame" / "Logs");
    static void Init(const std::filesystem::path& logDir);

    // Flush/close the log file (safe to call multiple times).
    static void Shutdown();

    // Primary logging interface (kept from your original header).
    static void Info(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Error(const std::string& msg);

    // Convenience overloads (non-breaking): forward to the std::string versions.
    static inline void Info(std::string_view msg)  { Info(std::string(msg)); }
    static inline void Warn(std::string_view msg)  { Warn(std::string(msg)); }
    static inline void Error(std::string_view msg) { Error(std::string(msg)); }

    static inline void Info(const char* msg)  { Info(std::string(msg ? msg : "")); }
    static inline void Warn(const char* msg)  { Warn(std::string(msg ? msg : "")); }
    static inline void Error(const char* msg) { Error(std::string(msg ? msg : "")); }

private:
    // Internal write primitive used by Info/Warn/Error.
    static void Write(const char* level, const std::string& msg);
};

} // namespace cg

// PR-10 style convenience macros (header-only, safe in expressions)
#ifndef CG_LOG_INFO
#define CG_LOG_INFO(msg)  ::cg::Log::Info((msg))
#endif
#ifndef CG_LOG_WARN
#define CG_LOG_WARN(msg)  ::cg::Log::Warn((msg))
#endif
#ifndef CG_LOG_ERROR
#define CG_LOG_ERROR(msg) ::cg::Log::Error((msg))
#endif
