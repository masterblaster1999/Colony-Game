#pragma once
#include <Windows.h>

namespace CrashHandler {
  // Call once at startup. dumpDir can be L"." to drop next to the exe.
  void Install(const wchar_t* dumpDir = L".");
}
