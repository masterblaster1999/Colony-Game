#include "platform/win/LauncherCliWin.h"

#include <shellapi.h>   // CommandLineToArgvW
#include <cwchar>       // _wcsicmp, _wcsnicmp
#include <windows.h>    // GetCommandLineW

// === MOVE the bodies from WinLauncher.cpp ===
//
// static std::wstring QuoteArgWindows(const std::wstring& arg) { ... }
// static std::wstring BuildChildArguments() { ... }
// static bool TryGetArgValue(const wchar_t* name, std::wstring& out) { ... }
// static bool HasFlag(const wchar_t* name) { ... }
//
// Again, remove `static` when you move them so they match the header.
