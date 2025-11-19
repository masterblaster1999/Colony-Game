// CrashDumpWin.cpp
#include "CrashDumpWin.h"
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

CrashDumpGuard::CrashDumpGuard(const wchar_t* appName) noexcept
    : _appName(appName)
{
    _prevFilter = ::SetUnhandledExceptionFilter(&CrashDumpGuard::UnhandledExceptionFilter);
}

CrashDumpGuard::~CrashDumpGuard()
{
    // Restore previous filter (optional; harmless to leave as-is if you prefer)
    if (_prevFilter) {
        ::SetUnhandledExceptionFilter(_prevFilter);
    }
}

LONG WINAPI CrashDumpGuard::UnhandledExceptionFilter(EXCEPTION_POINTERS* info) noexcept
{
    // Very simple example: write a minidump next to the EXE.
    // In a real game, you’d add more robust file naming & error handling here.

    wchar_t exePath[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Replace .exe with .dmp (very rough, but fine for now)
    wchar_t dumpPath[MAX_PATH] = {};
    wcscpy_s(dumpPath, exePath);
    wchar_t* ext = wcsrchr(dumpPath, L'.');
    if (ext) {
        wcscpy_s(ext, 5, L".dmp");
    }

    HANDLE hFile = ::CreateFileW(
        dumpPath,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId = ::GetCurrentThreadId();
        dumpInfo.ExceptionPointers = info;
        dumpInfo.ClientPointers = FALSE;

        ::MiniDumpWriteDump(
            ::GetCurrentProcess(),
            ::GetCurrentProcessId(),
            hFile,
            MiniDumpWithIndirectlyReferencedMemory,
            &dumpInfo,
            nullptr,
            nullptr);

        ::CloseHandle(hFile);
    }

    // Let the OS continue its normal unhandled-exception handling
    return EXCEPTION_EXECUTE_HANDLER;
}
