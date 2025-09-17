#define WIN32_LEAN_AND_MEAN
#include "CrashHandler.h"
#include <windows.h>
#include <dbghelp.h>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

static std::wstring g_dumpDir;

static void EnsureDir(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
}

static bool WriteMiniDump(EXCEPTION_POINTERS* pInfo) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf(path, L"%s\\ColonyGame_%04u%02u%02u_%02u%02u%02u.dmp",
             g_dumpDir.c_str(), st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ClientPointers = FALSE;
    mei.ExceptionPointers = pInfo;
    mei.ThreadId = GetCurrentThreadId();

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
                                &mei, nullptr, nullptr);
    CloseHandle(hFile);
    return ok == TRUE;
}

LONG WINAPI TopLevelExceptionHandler(EXCEPTION_POINTERS* pInfo) {
    WriteMiniDump(pInfo);
    return EXCEPTION_EXECUTE_HANDLER; // allow a clean exit after writing the dump
}

void InitCrashHandler(const wchar_t* dumpDir) {
    g_dumpDir = dumpDir;
    EnsureDir(g_dumpDir);
    SetUnhandledExceptionFilter(TopLevelExceptionHandler);
}
