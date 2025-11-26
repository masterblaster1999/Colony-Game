#include "CrashHandler.h"
#include <windows.h>
#include <dbghelp.h>
#include <pathcch.h>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS* e) {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathCchRemoveFileSpec(exePath, MAX_PATH);
    wchar_t dumpPath[MAX_PATH]{};
    PathCchCombine(dumpPath, MAX_PATH, exePath, L"crash.dmp");

    HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{ GetCurrentThreadId(), e, FALSE };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpWithDataSegs | MiniDumpWithThreadInfo,
                          &info, nullptr, nullptr);  // Microsoft API: MiniDumpWriteDump
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() {
    // Best practice: avoid modal error dialogs; also set a top-level SEH filter.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);  // no WER UI. :contentReference[oaicite:0]{index=0}
    SetUnhandledExceptionFilter(UnhandledExceptionFilterFn);       // top-level SEH. :contentReference[oaicite:1]{index=1}
}
