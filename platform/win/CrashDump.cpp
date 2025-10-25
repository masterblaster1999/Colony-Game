#include <Windows.h>
#include <ShlObj.h>           // SHGetKnownFolderPath
#include <KnownFolders.h>     // FOLDERID_LocalAppData
#include <combaseapi.h>       // CoTaskMemFree
#include <DbgHelp.h>
#include <filesystem>
#include <fstream>

// Linked via CMake; no #pragma comment needed.

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
