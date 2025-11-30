#include "CrashHandler.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <filesystem>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI ColonyUnhandledFilter(EXCEPTION_POINTERS* ep) {
    // %LOCALAPPDATA%\ColonyGame\Crashes\YYYYMMDD_HHMMSS.dmp
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path);
    std::filesystem::path dir = std::filesystem::path(path) / L"ColonyGame" / L"Crashes";
    std::filesystem::create_directories(dir);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[128];
    swprintf_s(name, L"%04u%02u%02u_%02u%02u%02u.dmp",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    auto full = dir / name;

    HANDLE hFile = CreateFileW(full.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

namespace winplat {
void InstallCrashHandler(const wchar_t* /*gameName*/) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(ColonyUnhandledFilter);
}
}
