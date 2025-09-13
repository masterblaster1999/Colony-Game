#pragma once
#include <string>

namespace winqol {
    // Call once, at app start (very early, before creating threads).
    void InstallCrashHandler(const std::wstring& appName,
                             const std::wstring& appVersion);

    // Optional: call on clean shutdown if you want to flush logs explicitly.
    void UninstallCrashHandler();

    // Lightweight logger (thread-safe for simple lines).
    void LogLine(const std::wstring& line);
}
