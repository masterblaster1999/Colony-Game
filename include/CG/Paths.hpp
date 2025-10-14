// include/CG/Paths.hpp
#pragma once
#include <filesystem>

namespace cg::paths {
  std::filesystem::path LocalAppDataRoot();     // %LOCALAPPDATA%\ColonyGame
  std::filesystem::path LogsDir();
  std::filesystem::path CrashDumpsDir();
  std::filesystem::path SavesDir();
  std::filesystem::path ConfigDir();
  void EnsureCreated(const std::filesystem::path& p);
}
