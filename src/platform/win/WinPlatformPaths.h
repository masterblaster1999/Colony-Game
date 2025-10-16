#pragma once
#ifndef _WIN32
# error "WinPlatformPaths is Windows-only."
#endif

// Windows lean headers
#ifndef NOMINMAX
# define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <knownfolders.h>    // KNOWNFOLDERID
#include <shlobj_core.h>     // SHGetKnownFolderPath
#include <combaseapi.h>      // CoTaskMemFree

#include <filesystem>
#include <string>
#include <string_view>

namespace winpath {

// Mutable configuration captured at init.
struct AppId {
    std::wstring company;   // e.g. L"ColonyWorks"
    std::wstring product;   // e.g. L"Colony-Game"
};

// Standard application directories (resolved & cached).
struct AppPaths {
    std::filesystem::path exePath;      // ...\Game.exe
    std::filesystem::path exeDir;       // ...\ (folder containing exe)
    std::filesystem::path workingDir;   // current working directory after Init
    std::filesystem::path localAppData; // %LocalAppData%
    std::filesystem::path roamingAppData; // %AppData%
    std::filesystem::path tempDir;      // %TEMP%
    // App-scoped subdirs under %LocalAppData%\Company\Product
    std::filesystem::path appHome;      // ...\Local\Company\Product
    std::filesystem::path logsDir;      // ...\Local\Company\Product\Logs
    std::filesystem::path savesDir;     // ...\Local\Company\Product\Saves
    std::filesystem::path cacheDir;     // ...\Local\Company\Product\Cache
    std::filesystem::path configDir;    // ...\Local\Company\Product\Config
};

// ---- Initialization ---------------------------------------------------------

// Initialize once, early in WinMain: resolves/creates directories, sets
// working-directory to the exe directory (before you spin up worker threads).
// Returns false on a hard error (rare). Non-throwing.
bool Init(const AppId& app);

// Access the cached paths (after Init()).
const AppPaths& Paths();

// ---- Utilities --------------------------------------------------------------

// Make sure a directory tree exists (non-throwing).
bool EnsureDir(const std::filesystem::path& p);

// Add an extra directory to the process DLL search path (if supported by OS).
// Returns true if successfully added (or not needed), false otherwise.
bool AddDllSearchDir(const std::filesystem::path& dirAbs);

// UTF conversions (for logs etc.).
std::wstring Utf8ToWide(std::string_view utf8);
std::string  WideToUtf8(std::wstring_view wide);

// Low-level helpers (exposed for tests/tooling)
std::filesystem::path GetExecutablePath();
std::filesystem::path GetExecutableDir();
std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID id); // throws on failure
std::filesystem::path GetTempFolder(); // never throws; may be empty on error

} // namespace winpath
