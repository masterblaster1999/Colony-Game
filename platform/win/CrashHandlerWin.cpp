#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include "platform/win/PathUtilWin.h"

#pragma comment(lib, "Dbghelp.lib")
namespace fs = std::filesystem;

static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  SYSTEMTIME st{}; GetLocalTime(&st);
  wchar_t name[64];
  swprintf_s(name, L"%04u%02u%02u-%02u%02u%02u.dmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

  fs::path dir = winpath::saved_games_dir(L"Colony Game") / L"Crashes";
  std::error_code ec; fs::create_directories(dir, ec);
  fs::path dumpPath = dir / name;

  HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithProcessThreadData |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                      hFile, type, &mei, nullptr, nullptr);
    CloseHandle(hFile);
  }
  return EXCEPTION_EXECUTE_HANDLER; // let the process die; dump is written
}

namespace wincrash {
  void InitCrashHandler(const wchar_t* /*appName*/) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(UnhandledFilter);
  }
}
