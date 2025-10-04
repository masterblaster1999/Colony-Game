// platform/win/CrashDumpStub.cpp
// Windows‑first no‑op implementation that matches CrashDumpWin.h exactly.
// Purpose: keep linkers happy while the real CrashDumpWin.cpp is disabled.

// This file is safe to remove once you enable the real implementation.
// It intentionally does *nothing* but preserves API/ABI so game code can call it.

#include "CrashDumpWin.h"

#include <cstdarg>   // va_list
#include <mutex>

namespace CrashDumpWin {

namespace {
  // Minimal internal state so setters have somewhere to land.
  // (Purely to avoid static analysis warnings; not used.)
  std::mutex g_mutex;

  // Dump type is Windows-only; use int placeholder off Windows.
#if defined(_WIN32)
  MINIDUMP_TYPE g_dumpType =
      (MINIDUMP_TYPE)(MiniDumpWithThreadInfo |
                      MiniDumpWithIndirectlyReferencedMemory |
                      MiniDumpScanMemory);
#else
  int           g_dumpType = 0;
#endif

  int           g_dumpLevel        = 2;   // Balanced
  int           g_postCrashAction  = 1;   // ExitProcess
  unsigned long g_maxKeep          = 10;  // keep last N dumps
  unsigned long g_throttleSec      = 3;   // collapse storms
  bool          g_skipIfDebugger   = true;
  bool          g_sidecarMetadata  = true;

  const wchar_t* g_extraComment    = nullptr;

  LogTailCallback g_logTailCb      = nullptr;
  void*           g_logTailUser    = nullptr;
  size_t          g_logTailMax     = 0;

  // swallow varargs safely
  inline void swallow_va_(const wchar_t* /*fmt*/, va_list& args) { (void)args; }
} // namespace

// ---------------------------- Core API ----------------------------

#if defined(_WIN32)
bool Init(const wchar_t* /*appName*/,
          const wchar_t* /*dumpDir*/,
          const wchar_t* /*buildTag*/) {
  // No-op: pretend we initialized successfully.
  return true;
}

bool WriteManualDump(const wchar_t* /*reason*/) {
  // No-op: report success so callers can continue paths gated on return value.
  return true;
}

void SetDumpType(MINIDUMP_TYPE type) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_dumpType = type;
}

void Shutdown() {
  // No-op
}
#else // !defined(_WIN32) — keep non-Windows tools buildable if ever needed.

bool Init(const wchar_t*, const wchar_t*, const wchar_t*) { return true; }
bool WriteManualDump(const wchar_t*) { return true; }
void SetDumpType(int /*placeholder*/) {}
void Shutdown() {}

#endif // _WIN32

// ---------------------------- Optional API ----------------------------

void SetDumpLevel(int level) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_dumpLevel = level;
}

void SetPostCrashAction(int action) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_postCrashAction = action;
}

void SetMaxDumpsToKeep(unsigned long n) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_maxKeep = n;
}

void SetThrottleSeconds(unsigned long seconds) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_throttleSec = seconds;
}

void SetSkipIfDebuggerPresent(bool skip) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_skipIfDebugger = skip;
}

void SetExtraCommentLine(const wchar_t* line) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_extraComment = line; // stored pointer only (no allocation) on purpose
}

void SetCrashKey(const wchar_t* /*key*/, const wchar_t* /*value*/) {}
void RemoveCrashKey(const wchar_t* /*key*/) {}
void ClearCrashKeys() {}

void AddBreadcrumb(const wchar_t* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  swallow_va_(fmt, args);
  va_end(args);
}

void SetBreadcrumbCapacity(unsigned /*cap*/) {}

void SetLogTailCallback(LogTailCallback cb, void* user, size_t maxBytes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_logTailCb  = cb;
  g_logTailUser = user;
  g_logTailMax = maxBytes;
}

void EnableSidecarMetadata(bool enable) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sidecarMetadata = enable;
}

void SetPreDumpCallback(void (*)(void)) {}
void SetPostDumpCallback(void (*)(const wchar_t*, bool)) {}

bool ConfigureWERLocalDumps(const wchar_t* /*exeName*/,
                            const wchar_t* /*dumpFolder*/,
                            unsigned long  /*dumpType*/,
                            unsigned long  /*dumpCount*/) {
  // Not implemented in the stub.
  return false;
}

void SimulateCrash() {
  // Intentionally do nothing in the stub.
  // (Real implementation might raise a SEH exception to test the pipeline.)
}

} // namespace CrashDumpWin
