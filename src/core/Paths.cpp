// src/core/Paths.cpp
#include "CG/Paths.hpp"
#include <Windows.h>
#include <ShlObj_core.h>
#include <KnownFolders.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace {
  std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &wpath)) && wpath) {
      std::filesystem::path p = wpath;
      CoTaskMemFree(wpath);
      return p;
    }
    // Fallback to %LOCALAPPDATA%
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0)
      return std::filesystem::path(buf);
    return std::filesystem::temp_directory_path();
  }

  const std::wstring kVendor = L"ColonyGame";
}

namespace cg::paths {
  std::filesystem::path LocalAppDataRoot() {
    return KnownFolder(FOLDERID_LocalAppData) / kVendor;
  }
  std::filesystem::path LogsDir()       { return LocalAppDataRoot() / L"logs"; }
  std::filesystem::path CrashDumpsDir() { return LocalAppDataRoot() / L"crashes"; }
  std::filesystem::path SavesDir()      { return LocalAppDataRoot() / L"saves"; }
  std::filesystem::path ConfigDir()     { return LocalAppDataRoot() / L"config"; }

  void EnsureCreated(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
  }
}
