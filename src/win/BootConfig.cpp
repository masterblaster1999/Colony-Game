// src/win/BootConfig.cpp
#include <windows.h>
#include <string>
struct BootConfig {
  std::wstring presentMode;   // "flip_discard" | "blt" | ...
  bool allowTearing = false;
  double fixedDtMs = 0.0;     // 0 = variable
  double maxFrameMs = 250.0;
};

static std::wstring ReadEnvW(const wchar_t* key) {
  wchar_t buf[256]; DWORD n = GetEnvironmentVariableW(key, buf, 256);
  return (n > 0 && n < 256) ? std::wstring(buf, n) : L"";
}

BootConfig GetBootConfigFromEnv() {
  BootConfig cfg;
  cfg.presentMode = ReadEnvW(L"COLONY_PRESENT_MODE");
  if (auto s = ReadEnvW(L"COLONY_PRESENT_ALLOW_TEARING"); !s.empty()) cfg.allowTearing = (s == L"1");
  if (auto s = ReadEnvW(L"COLONY_SIM_FIXED_DT_MS"); !s.empty())       cfg.fixedDtMs = std::stod(std::string(s.begin(), s.end()));
  if (auto s = ReadEnvW(L"COLONY_SIM_MAX_FRAME_MS"); !s.empty())      cfg.maxFrameMs = std::stod(std::string(s.begin(), s.end()));
  return cfg;
}
