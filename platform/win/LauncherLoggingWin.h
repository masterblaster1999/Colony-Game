// platform/win/LauncherLoggingWin.h
#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

// Logging helpers used by the launcher (log files under %LOCALAPPDATA%\ColonyGame\logs).
std::filesystem::path LogsDir();
std::wofstream         OpenLogFile();

// Overload for the real log file
void WriteLog(std::wofstream& log, const std::wstring& line);

// Overload for arbitrary wide streams (used by embedded safe-mode)
void WriteLog(std::wostream& log, const std::wstring& line);
