#pragma once
#include <filesystem>
#include <windows.h>

namespace cg::winenv {
inline std::filesystem::path exe_dir() {
  wchar_t buf[MAX_PATH]{};
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::filesystem::path p = (n ? std::filesystem::path(buf) : std::filesystem::path(L"."));
  return p.remove_filename();
}

inline std::filesystem::path resource_dir() {
  // You can customize: e.g., return exe_dir() / L"res";
  return exe_dir();
}
}
