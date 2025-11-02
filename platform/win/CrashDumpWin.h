#pragma once
//
// CrashDumpWin.h
//
// Public interface for a robust Windows minidump facility.
// Compatible with the advanced CrashDumpWin.cpp provided separately.
//
// License: Public domain / CC0-style. Use at your own risk.
//
// -----------------------------------------------------------------------------
// Core API (always available on Windows builds; no-ops or stubs on others)
// -----------------------------------------------------------------------------
//   bool Init(const wchar_t* appName=nullptr,
//             const wchar_t* dumpDir=nullptr,
//             const wchar_t* buildTag=nullptr);
//   bool WriteManualDump(const wchar_t* reason=L"Manual");
//   void SetDumpType(MINIDUMP_TYPE type);        // Windows: MINIDUMP_TYPE
//   void Shutdown();
//
// -----------------------------------------------------------------------------
// Optional helpers (enable faster diagnostics and better field telemetry)
// Add the prototypes you need in other TUs; all are implemented in the .cpp:
//   // Levels: 0=Tiny, 1=Small, 2=Balanced (default), 3=Heavy, 4=Full
//   void SetDumpLevel(int level);
//   // Strongly-typed convenience enum + inline wrapper also provided below.
//
//   // Post-crash behavior: 0=Return, 1=ExitProcess (default), 2=TerminateProcess
//   void SetPostCrashAction(int action);
//   // Strongly-typed convenience enum + inline wrapper also provided below.
//
//   void SetMaxDumpsToKeep(DWORD n);            // retention (default 10)
//   void SetThrottleSeconds(DWORD seconds);     // collapse storms (default 3s)
//   void SetSkipIfDebuggerPresent(bool skip);   // default true
//
//   void SetExtraCommentLine(const wchar_t* line); // extra line in comment stream
//
//   void SetCrashKey(const wchar_t* key, const wchar_t* value);
//   void RemoveCrashKey(const wchar_t* key);
//   void ClearCrashKeys();
//
//   void AddBreadcrumb(const wchar_t* fmt, ...);     // thread-safe ring buffer
//   void SetBreadcrumbCapacity(unsigned cap);        // default 64
//
//   typedef size_t (*LogTailCallback)(void* user, char* dst, size_t capBytes);
//   void SetLogTailCallback(LogTailCallback cb, void* user, size_t maxBytes); // UTF-8
//
//   void EnableSidecarMetadata(bool enable);    // default true (.txt file)
//
//   void SetPreDumpCallback(void (*fn)());      // e.g., flush logs
//   void SetPostDumpCallback(void (*fn)(const wchar_t* path, bool ok));
//
//   // Windows Error Reporting (WER) LocalDumps helper (HKCU):
//   bool ConfigureWERLocalDumps(const wchar_t* exeName,
//                               const wchar_t* dumpFolder,
//                               DWORD dumpType /*1=minidump, 2=full*/,
//                               DWORD dumpCount /*e.g., 10*/);
//
//   void SimulateCrash();                        // test the pipeline
//
// -----------------------------------------------------------------------------
// Environment overrides (optional; read at Init):
//   CRASHDUMP_DIR             = <folder>
//   CRASHDUMP_MAX             = <N>     (keep last N dumps)
//   CRASHDUMP_THROTTLE_SEC    = <secs>  (default 3)
//   CRASHDUMP_SKIP_DEBUGGER   = 0|1     (default 1)
//   CRASHDUMP_FULLMEM         = 0|1     (adds FullMemory flags)
//   CRASHDUMP_POST            = return | exit | terminate
// -----------------------------------------------------------------------------

#include <cstddef> // size_t

// Export macro (optional: define CRASHDUMPWIN_BUILD_DLL / CRASHDUMPWIN_USE_DLL)
#if !defined(CRASHDUMPWIN_API)
#  if defined(CRASHDUMPWIN_BUILD_DLL)
#    define CRASHDUMPWIN_API __declspec(dllexport)
#  elif defined(CRASHDUMPWIN_USE_DLL)
#    define CRASHDUMPWIN_API __declspec(dllimport)
#  else
#    define CRASHDUMPWIN_API
#  endif
#endif

#if defined(_WIN32)
  #define CRASHDUMPWIN_ENABLED 1
#else
  #define CRASHDUMPWIN_ENABLED 0
#endif

#if CRASHDUMPWIN_ENABLED
  // Replaces local macro defines with a single, guarded include.
  // WinCommon.h defines WIN32_LEAN_AND_MEAN/NOMINMAX only if not already defined,
  // then includes <windows.h>.
  #include "platform/win/WinCommon.h"
  #include <DbgHelp.h> // for MINIDUMP_TYPE and MINIDUMP_* flags
#else
  // Minimal Windows types for cross-platform consumers invoking no-op stubs.
  typedef unsigned long DWORD;
#endif

namespace CrashDumpWin {

// =============================== Core API ===============================

#if CRASHDUMPWIN_ENABLED
CRASHDUMPWIN_API bool Init(const wchar_t* appName = nullptr,
                           const wchar_t* dumpDir = nullptr,
                           const wchar_t* buildTag = nullptr);

CRASHDUMPWIN_API bool WriteManualDump(const wchar_t* reason = L"Manual");

// Windows signature uses MINIDUMP_TYPE:
CRASHDUMPWIN_API void SetDumpType(MINIDUMP_TYPE type);

CRASHDUMPWIN_API void Shutdown();

#else // !CRASHDUMPWIN_ENABLED

// On non-Windows platforms, these are *declared* so your non-Windows TU can link
// against a stub .cpp if you keep one. If you prefer header-only no-ops here,
// you can convert these to inline definitions that return/do nothing.
CRASHDUMPWIN_API bool Init(const wchar_t* appName = nullptr,
                           const wchar_t* dumpDir = nullptr,
                           const wchar_t* buildTag = nullptr);
CRASHDUMPWIN_API bool WriteManualDump(const wchar_t* reason = L"Manual");

// Use an int placeholder to avoid MINIDUMP_TYPE outside Windows:
CRASHDUMPWIN_API void SetDumpType(int /*placeholder*/);

CRASHDUMPWIN_API void Shutdown();

#endif // CRASHDUMPWIN_ENABLED

// ========================== Optional: convenience enums ==========================

/// Dump detail presets.
/// Use with the inline wrapper SetDumpLevel(DumpLevel) below,
/// or call SetDumpLevel(int) directly with 0..4.
enum class DumpLevel {
    Tiny      = 0,
    Small     = 1,
    Balanced  = 2, // default
    Heavy     = 3,
    Full      = 4
};

/// What to do after generating a dump in an unhandled exception path.
enum class PostCrashAction {
    Return            = 0, // return to caller; process may continue (generally unsafe)
    ExitProcess       = 1, // default
    TerminateProcess  = 2
};

// =============================== Optional API ===============================

CRASHDUMPWIN_API void SetDumpLevel(int level);                  // 0..4 (see enum)
CRASHDUMPWIN_API void SetPostCrashAction(int action);           // 0..2 (see enum)

CRASHDUMPWIN_API void SetMaxDumpsToKeep(DWORD n);               // rotate old dumps
CRASHDUMPWIN_API void SetThrottleSeconds(DWORD seconds);        // collapse storms
CRASHDUMPWIN_API void SetSkipIfDebuggerPresent(bool skip);

CRASHDUMPWIN_API void SetExtraCommentLine(const wchar_t* line);

CRASHDUMPWIN_API void SetCrashKey(const wchar_t* key, const wchar_t* value);
CRASHDUMPWIN_API void RemoveCrashKey(const wchar_t* key);
CRASHDUMPWIN_API void ClearCrashKeys();

CRASHDUMPWIN_API void AddBreadcrumb(const wchar_t* fmt, ...);
CRASHDUMPWIN_API void SetBreadcrumbCapacity(unsigned cap);

typedef size_t (*LogTailCallback)(void* user, char* dst, size_t capBytes);
CRASHDUMPWIN_API void SetLogTailCallback(LogTailCallback cb, void* user, size_t maxBytes);

CRASHDUMPWIN_API void EnableSidecarMetadata(bool enable);

CRASHDUMPWIN_API void SetPreDumpCallback(void (*fn)());
CRASHDUMPWIN_API void SetPostDumpCallback(void (*fn)(const wchar_t* path, bool ok));

CRASHDUMPWIN_API bool ConfigureWERLocalDumps(const wchar_t* exeName,
                                             const wchar_t* dumpFolder,
                                             DWORD dumpType /*1=minidump, 2=full*/,
                                             DWORD dumpCount /*e.g., 10*/);

CRASHDUMPWIN_API void SimulateCrash();

// ========================== Inline convenience wrappers ==========================

/// Strongly-typed overloads that forward to the int‑based implementations.
/// These are header‑only and do not add link requirements.
inline void SetDumpLevel(DumpLevel level) {
    SetDumpLevel(static_cast<int>(level));
}
inline void SetPostCrashAction(PostCrashAction action) {
    SetPostCrashAction(static_cast<int>(action));
}

#if !CRASHDUMPWIN_ENABLED
// ------------------- Non-Windows fallbacks (optional) -------------------
// If you do NOT compile/link the non-Windows .cpp stub, uncomment the
// following inline no-op implementations to make this header self-contained
// off Windows. Keeping them commented avoids duplicate symbols if you do
// provide a .cpp stub elsewhere.
/*
inline bool Init(const wchar_t*, const wchar_t*, const wchar_t*) { return false; }
inline bool WriteManualDump(const wchar_t*) { return false; }
inline void SetDumpType(int) {}
inline void Shutdown() {}

inline void SetDumpLevel(int) {}
inline void SetPostCrashAction(int) {}
inline void SetMaxDumpsToKeep(DWORD) {}
inline void SetThrottleSeconds(DWORD) {}
inline void SetSkipIfDebuggerPresent(bool) {}
inline void SetExtraCommentLine(const wchar_t*) {}
inline void SetCrashKey(const wchar_t*, const wchar_t*) {}
inline void RemoveCrashKey(const wchar_t*) {}
inline void ClearCrashKeys() {}
inline void AddBreadcrumb(const wchar_t*, ...) {}
inline void SetBreadcrumbCapacity(unsigned) {}
inline void SetLogTailCallback(LogTailCallback, void*, size_t) {}
inline void EnableSidecarMetadata(bool) {}
inline void SetPreDumpCallback(void (*)()) {}
inline void SetPostDumpCallback(void (*)(const wchar_t*, bool)) {}
inline bool ConfigureWERLocalDumps(const wchar_t*, const wchar_t*, DWORD, DWORD) { return false; }
inline void SimulateCrash() {}
*/
#endif // !CRASHDUMPWIN_ENABLED

} // namespace CrashDumpWin
