// src/crash/CrashDumpWin.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <filesystem>
#pragma comment(lib, "Dbghelp.lib")

namespace {
LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  // Create dumps/<ts>.dmp
  SYSTEMTIME st; GetLocalTime(&st);
  wchar_t fname[256];
  swprintf(fname, 256, L"crashdumps\\Colony_%04d%02d%02d_%02d%02d%02d.dmp",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  std::filesystem::create_directories(L"crashdumps");

  HANDLE hFile = CreateFileW(fname, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = ep;
  mei.ClientPointers = FALSE;

  MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
      MiniDumpWithThreadInfo |
      MiniDumpWithUnloadedModules |
      MiniDumpWithIndirectlyReferencedMemory |
      MiniDumpWithHandleData |
      MiniDumpScanMemory);

  MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mei, nullptr, nullptr);
  FlushFileBuffers(hFile);
  CloseHandle(hFile);

  // Let WER also collect if configured
  return EXCEPTION_EXECUTE_HANDLER;
}
}

void InstallCrashHandler(const wchar_t* /*dumpFolder*/) {
  SetUnhandledExceptionFilter(UnhandledFilter);
}
