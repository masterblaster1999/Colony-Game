#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>      // SHGetKnownFolderPath
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace winplat {

namespace fs = std::filesystem;

// UTF‑8 / UTF‑16 helpers
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& w);

// Executable and working directory helpers
fs::path GetExecutablePath();
fs::path GetExecutableDir();
bool     SetCurrentDirToExe();

// Known folders / app data
fs::path KnownFolder(REFKNOWNFOLDERID id);
fs::path AppDataRoot(const std::wstring& appName);  // %LOCALAPPDATA%\appName
fs::path LogsDir(const std::wstring& appName);      // ...\logs
fs::path SavesDir(const std::wstring& appName);     // ...\saves
fs::path ConfigDir(const std::wstring& appName);    // ...\config
fs::path CrashDumpDir(const std::wstring& appName); // ...\crashdumps

// File I/O
bool ReadFileBinary(const fs::path& p, std::vector<uint8_t>& outData);
bool WriteFileBinary(const fs::path& p, const uint8_t* data, size_t size);
inline bool WriteFileBinary(const fs::path& p, const std::vector<uint8_t>& data) { return WriteFileBinary(p, data.data(), data.size()); }

// Misc
bool EnsureDir(const fs::path& dir);
std::wstring Win32ErrorMessage(DWORD err);

} // namespace winplat
