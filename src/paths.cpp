// src/paths.cpp
#include "paths.h"
#include <array>
#include <cstdlib>          // std::getenv
#include <system_error>

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  #include <vector>
#else
  #include <unistd.h>
  #include <vector>
#endif

using std::filesystem::path;

namespace paths {

static path slow_realpath(const path& p) {
  std::error_code ec;
  auto abs = std::filesystem::weakly_canonical(p, ec);
  return ec ? p : abs;
}

path exe_dir() {
#if defined(_WIN32)
  std::wstring buf(1'024, L'\0');
  for (;;) {
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) return {};
    if (len < buf.size()) { buf.resize(len); break; }
    buf.resize(buf.size() * 2);
  }
  return slow_realpath(path(buf).parent_path());
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buf(size);
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  return slow_realpath(path(buf.data()).parent_path());
#else   // Linux/Unix
  std::vector<char> buf(4096);
  ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size()-1);
  if (n <= 0) return {};
  buf[static_cast<size_t>(n)] = '\0';
  return slow_realpath(path(buf.data()).parent_path());
#endif
}

static bool looks_like_assets_dir(const path& p) {
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) return false;
  // Accept any of these as your “root”; adjust to your repo layout.
  static constexpr const char* sentinels[] = { "assets", "data", "resources" };
  for (auto* s : sentinels) {
    if (std::filesystem::exists(p / s, ec)) return true;
  }
  // …or if the directory itself IS the assets dir (contains common subfolders)
  static constexpr const char* common[] = { "textures", "audio", "shaders" };
  int hits = 0;
  for (auto* s : common) hits += std::filesystem::exists(p / s, ec) ? 1 : 0;
  return hits >= 2;
}

path find_assets_root() {
  std::error_code ec;

  if (const char* env = std::getenv("COLONY_GAME_ASSETS")) {
    path p = slow_realpath(path(env));
    if (looks_like_assets_dir(p)) return p;
  }

  const path exe = exe_dir();
  const std::array<path, 5> candidates = {
      exe / "assets",
      exe / "resources",
      exe,                                   // portable build with assets next to exe
      exe / ".." / "share" / "Colony-Game",  // CMake install tree
      std::filesystem::current_path()        // last resort: wherever we were launched
  };

  for (const auto& c : candidates) {
    if (looks_like_assets_dir(c)) {
      return slow_realpath(c);
    }
  }
  return {};
}

path assets(const path& rel) {
  static path root = find_assets_root();
  return root.empty() ? rel : slow_realpath(root / rel);
}

} // namespace paths
