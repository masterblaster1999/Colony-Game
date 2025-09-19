#pragma once
#include <filesystem>
#include <string>

namespace winenv {

// Directory containing the executable.
std::filesystem::path exe_dir();

// First ancestor that contains a "res" folder (project root).
std::filesystem::path project_root();

// <project_root>/res
std::filesystem::path resource_dir();

// %LOCALAPPDATA%\<appName>
std::filesystem::path user_data_dir(const std::wstring& appName = L"Colony-Game");

// Creates %LOCALAPPDATA%\<appName>\{saves,logs}
void ensure_user_dirs(const std::wstring& appName = L"Colony-Game");

// Call this EARLY in WinMain/main on Windows.
// - Locks down DLL search path
// - Adjusts working directory to project root
// - Sets Per-Monitor-V2 DPI
// - Checks for res/ and shows a useful error if missing
void init_process_environment(const std::wstring& appName = L"Colony-Game");

// Tiny helper to send a line to the debugger (and optionally a log file later)
void log_debug(const std::wstring& line);

} // namespace winenv
