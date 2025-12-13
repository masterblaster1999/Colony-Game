// platform/win/CrashDump.cpp
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* info) {
    HANDLE hFile = CreateFileA("ColonyGameCrash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei { GetCurrentThreadId(), info, FALSE };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpWithIndirectlyReferencedMemory, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() {
    SetUnhandledExceptionFilter(TopLevelFilter);
}
