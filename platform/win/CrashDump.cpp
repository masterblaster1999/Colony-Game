#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>
#include <shlobj.h>        // SHGetKnownFolderPath
#include <KnownFolders.h>  // FOLDERID_LocalAppData
#include <combaseapi.h>    // CoTaskMemFree
#include <filesystem>
#include <fstream>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Ole32.lib")    // for CoTaskMemFree
#pragma comment(lib, "Shell32.lib")  // for SHGetKnownFolderPath

namespace fs = std::filesystem;

static LONG WINAPI UnhandledHandler(EXCEPTION_POINTERS* info) {
    // Create folder: %LOCALAPPDATA%\ColonyGame\crashes
    PWSTR appData = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &appData))) {
        dir = fs::path(appData) / L"ColonyGame" / L"crashes";
        CoTaskMemFree(appData);
    }
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Dump filename with timestamp
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"%04d%02d%02d-%02d%02d%02d.dmp",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE hFile = CreateFileW((dir / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    // Let Windows show its crash UI too (or return EXCEPTION_EXECUTE_HANDLER to swallow)
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace crashdump {
    bool install() {
        return SetUnhandledExceptionFilter(UnhandledHandler) != nullptr;
    }
}
