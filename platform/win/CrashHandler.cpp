#include "CrashHandler.h"
#include <DbgHelp.h>
#include <filesystem>
#include <string>
#pragma comment(lib, "DbgHelp.lib")

static std::wstring g_dumpDir;

static LONG WINAPI ColonyUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  ::CreateDirectoryW(g_dumpDir.c_str(), nullptr);
  SYSTEMTIME st; GetLocalTime(&st);
  wchar_t name[256];
  swprintf_s(name, L"ColonyCrash_%04d%02d%02d_%02d%02d%02d.dmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  std::filesystem::path path = std::filesystem::path(g_dumpDir) / name;

  HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    ::MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                        MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
                        &mei, nullptr, nullptr);
    ::CloseHandle(hFile);
  }
  return EXCEPTION_EXECUTE_HANDLER; // let OS terminate
}

void CrashHandler::Install(const wchar_t* dumpDir) {
  g_dumpDir = dumpDir ? dumpDir : L".";
  ::SetUnhandledExceptionFilter(&ColonyUnhandledExceptionFilter);
}
