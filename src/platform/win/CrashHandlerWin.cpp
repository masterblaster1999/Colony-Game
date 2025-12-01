#include <windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <cwchar>               // for swprintf_s
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st; GetLocalTime(&st);

    // Build a timestamped dump path in %TEMP% (fallback to ".")
    wchar_t path[MAX_PATH];
    const wchar_t* temp = _wgetenv(L"TEMP");
    swprintf_s(path, sizeof(path)/sizeof(path[0]),
               L"%s\\ColonyCrash_%04u%02u%02u_%02u%02u%02u.dmp",
               temp ? temp : L".",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), ep, FALSE };

        // Make the DumpType explicitly a MINIDUMP_TYPE (fixes C2664)
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory
        );

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          dumpType,            // typed enum, not int
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
