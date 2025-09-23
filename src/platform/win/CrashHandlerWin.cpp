#include "CrashHandlerWin.h"
#include <windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <string>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* pInfo) {
    // Where to write the dump
    wchar_t path[MAX_PATH]{};
    SYSTEMTIME st{}; GetLocalTime(&st);
    swprintf_s(path, L"%s\\ColonyGame\\crash\\%04u%02u%02u-%02u%02u%02u.dmp",
               _wgetenv(L"LOCALAPPDATA"),
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ClientPointers = FALSE;
        mei.ExceptionPointers = pInfo;
        mei.ThreadId = GetCurrentThreadId();

        // Lightweight dump type that still has stacks, unloaded modules etc.
        MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
              MiniDumpWithIndirectlyReferencedMemory
            | MiniDumpWithUnloadedModules
            | MiniDumpWithThreadInfo);

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          type, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER; // Let the process die gracefully
}

void crashwin::install_minidump_writer(const wchar_t*) {
    SetUnhandledExceptionFilter(TopLevelFilter);
}
