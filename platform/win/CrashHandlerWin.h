#pragma once
namespace wincrash {
  // Initialize once at startup; creates Crashes\ folder under Saved Games
  void InitCrashHandler(const wchar_t* appName = L"Colony Game");
}
