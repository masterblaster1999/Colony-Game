#include <windows.h>
#include <DbgHelp.h>
#include <filesystem>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf(path, L"%s\\ColonyCrash_%04d%02d%02d_%02d%02d%02d.dmp",
             _wgetenv(L"TEMP"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), ep, FALSE };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    MessageBoxW(nullptr, L"The game crashed.\nA crash dump was saved to your Temp folder.",
                L"Colony-Game Crash", MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(UnhandledFilter);
}
