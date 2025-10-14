// src/platform/win/CrashHandler.h
#pragma once
#include <filesystem>

namespace cg::win {
  // Install a process-wide crash handler that drops a .dmp in dumpsDir.
  bool InstallCrashHandler(const std::filesystem::path& dumpsDir);
  void UninstallCrashHandler();
}
