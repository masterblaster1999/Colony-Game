// src/platform/win/CrashHandler.cpp
#include "CG/Paths.hpp"
#include "platform/win/CrashHandler.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <string>
#include <format>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

namespace {
  LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;

  std::wstring NowStamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    return std::format(L"{:04}-{:02}-{:02}_{:02}-{:02}-{:02}",
      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  }

  std::filesystem::path WriteDump(EXCEPTION_POINTERS* ep, const std::filesystem::path& dir) {
    std::filesystem::path out = dir / std::format(L"colony_{}.dmp", NowStamp());
    HANDLE hFile = CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    mdei.ThreadId          = GetCurrentThreadId();
    mdei.ExceptionPointers = ep;
    mdei.ClientPointers    = FALSE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                      MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
                      &mdei, nullptr, nullptr);

    CloseHandle(hFile);
    return out;
  }

  LONG WINAPI TopLevel(EXCEPTION_POINTERS* ep) {
    const auto dumps = cg::paths::CrashDumpsDir();
    cg::paths::EnsureCreated(dumps);
    const auto file = WriteDump(ep, dumps);
    if (!file.empty()) {
      MessageBoxW(nullptr,
        (L"Colony Game crashed.\nA crash dump was written to:\n" + file.wstring()).c_str(),
        L"Crash", MB_OK | MB_ICONERROR);
    }
    return EXCEPTION_EXECUTE_HANDLER;
  }
}

namespace cg::win {
  bool InstallCrashHandler(const std::filesystem::path& dumpsDir) {
    cg::paths::EnsureCreated(dumpsDir);
    g_prev = SetUnhandledExceptionFilter(TopLevel);
    return true;
  }
  void UninstallCrashHandler() {
    SetUnhandledExceptionFilter(g_prev);
  }
}
