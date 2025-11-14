#include "platform/win/LauncherLoggingWin.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <windows.h>         // OutputDebugStringW
#include "platform/win/PathUtilWin.h"

namespace fs = std::filesystem;

// === MOVE these from WinLauncher.cpp, unchanged ===
//
// static fs::path LogsDir() { ... }
// static std::wofstream OpenLogFile() { ... }
// static inline void WriteLog(std::wofstream&, const std::wstring&);
// static inline void WriteLog(std::wostream&, const std::wstring&);
//
// When moving them, drop the `static` keyword so they can be linked
// from WinLauncher.cpp, and keep the function bodies the same.
