// src/platform/win/CrashHandler.cpp
#include "CG/Paths.hpp"
#include "platform/win/CrashHandler.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <string>
#include <format>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

namespace {
  LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;
  LONG g_inTopLevel = 0;

  // Optional override: some callers want to control where dumps are written.
  // This must be set *before* installing the handler to be race-free.
  std::filesystem::path g_dumpsDir;
  std::atomic<bool> g_hasDumpsDir{false};

  std::filesystem::path EffectiveDumpDir() {
    if (g_hasDumpsDir.load(std::memory_order_acquire) && !g_dumpsDir.empty())
      return g_dumpsDir;
    return cg::paths::CrashDumpsDir();
  }

  std::wstring NowStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return std::format(L"{:04}-{:02}-{:02}_{:02}-{:02}-{:02}",
      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  }

  // Central place to define what kind of dump we want.
  // This uses a "rich" combination of MINIDUMP_TYPE flags, explicitly cast
  // to MINIDUMP_TYPE to satisfy the MiniDumpWriteDump signature.
  MINIDUMP_TYPE GetColonyDumpType() {
    return static_cast<MINIDUMP_TYPE>(
      MiniDumpWithFullMemory |
      MiniDumpWithHandleData |
      MiniDumpWithUnloadedModules |
      MiniDumpWithProcessThreadData |
      MiniDumpWithFullMemoryInfo |
      MiniDumpWithThreadInfo |
      MiniDumpWithModuleHeaders
    );
  }

  struct ScopedHandle {
    HANDLE h{};

    explicit ScopedHandle(HANDLE handle) : h(handle) {}

    ~ScopedHandle() {
      if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
      }
    }

    explicit operator bool() const {
      return h && h != INVALID_HANDLE_VALUE;
    }
  };

  std::filesystem::path WriteDump(EXCEPTION_POINTERS* ep, const std::filesystem::path& dir) {
    std::filesystem::path out = dir / std::format(L"colony_{}.dmp", NowStamp());

    ScopedHandle file(CreateFileW(
      out.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr));

    if (!file) {
      return {};
    }

    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    mdei.ThreadId          = GetCurrentThreadId();
    mdei.ExceptionPointers = ep;
    mdei.ClientPointers    = FALSE;

    const MINIDUMP_TYPE dumpType = GetColonyDumpType();

    BOOL ok = MiniDumpWriteDump(
      GetCurrentProcess(),
      GetCurrentProcessId(),
      file.h,
      dumpType,                // <- correct enum type, no more C2664
      ep ? &mdei : nullptr,
      nullptr,
      nullptr);

    if (!ok) {
      // Dump creation failed; return empty path so caller can handle/log if desired.
      return {};
    }

    return out;
  }

  LONG WINAPI TopLevel(EXCEPTION_POINTERS* ep) {
    // Simple re-entry guard: if we're already in the crash handler, don't recurse.
    if (InterlockedExchange(&g_inTopLevel, 1) != 0) {
      return EXCEPTION_EXECUTE_HANDLER;
    }

    const auto dumps = EffectiveDumpDir();
    cg::paths::EnsureCreated(dumps);
    const auto file = WriteDump(ep, dumps);

    if (!file.empty()) {
      const std::wstring msg =
        L"Colony Game crashed.\nA crash dump was written to:\n" + file.wstring();
      MessageBoxW(nullptr, msg.c_str(), L"Crash", MB_OK | MB_ICONERROR);
    }

    return EXCEPTION_EXECUTE_HANDLER;
  }
}

namespace cg::win {
  bool InstallCrashHandler(const std::filesystem::path& dumpsDir) {
    // Honor the caller-provided dump location. (Previous code ignored this arg
    // and always used cg::paths::CrashDumpsDir(), which was misleading.)
    const auto dir = dumpsDir.empty() ? cg::paths::CrashDumpsDir() : dumpsDir;
    g_dumpsDir = dir;
    g_hasDumpsDir.store(true, std::memory_order_release);

    cg::paths::EnsureCreated(dir);
    g_prev = SetUnhandledExceptionFilter(TopLevel);
    return true;
  }

  void UninstallCrashHandler() {
    SetUnhandledExceptionFilter(g_prev);
    g_hasDumpsDir.store(false, std::memory_order_release);
    g_dumpsDir.clear();
  }
}
