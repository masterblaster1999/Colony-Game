#include "CrashDump.h"
#include <windows.h>
#include <dbghelp.h>
#include <shlobj_core.h>
#include <filesystem>
#include <string>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* e)
{
    // Create %LOCALAPPDATA%\ColonyGame\Crashes\YYYYMMDD_HHMMSS.dmp
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
    {
        std::filesystem::path dumpDir = std::filesystem::path(localAppData) / L"ColonyGame" / L"Crashes";
        CoTaskMemFree(localAppData);
        std::error_code ec; std::filesystem::create_directories(dumpDir, ec);

        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t fname[128]{};
        swprintf_s(fname, L"%04u%02u%02u_%02u%02u%02u.dmp",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        auto dumpPath = dumpDir / fname;

        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = e;
            mei.ClientPointers = FALSE;

            // A small but useful dump
            MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory |
                MiniDumpWithThreadInfo);

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type,
                              &mei, nullptr, nullptr);
            CloseHandle(hFile);
        }
    }

    return EXCEPTION_EXECUTE_HANDLER; // allow process to terminate after dumping
}

void InstallCrashHandler(const wchar_t* /*productName*/)
{
    SetUnhandledExceptionFilter(TopLevelExceptionFilter);
}
