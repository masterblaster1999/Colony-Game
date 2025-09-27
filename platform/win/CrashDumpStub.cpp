// platform/win/CrashDumpStub.cpp
// Temporary, Windows-only stub to keep the build green while CrashDumpWin.cpp
// is refactored to avoid C2712 (__try/__except + object unwinding).
//
// Remove this file once CrashDumpWin.cpp is re-enabled in CMake.

#include "CrashDumpWin.h"

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace CrashDumpWin {

bool Initialize() noexcept {
  // No-op: pretend the crash dumper is installed.
  return true;
}

void Shutdown() noexcept {
  // No-op
}

bool SetDumpDirectory(const wchar_t* /*dir*/) noexcept {
  // No-op: accept any directory
  return true;
}

bool WriteProcessDump(DumpLevel /*level*/) noexcept {
  // No-op: say we succeeded so callers can continue.
  return true;
}

bool WriteProcessDumpTo(const wchar_t* /*path*/, DumpLevel /*level*/) noexcept {
  // No-op variant
  return true;
}

} // namespace CrashDumpWin
