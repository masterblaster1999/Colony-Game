#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <filesystem>
#include <string>
#include <cstdio>
#pragma comment(lib, "Dbghelp.lib") // works with MSVC; CMake also links Dbghelp

namespace {
    const wchar_t* g_appName = L"ColonyGame";
    std::filesystem::path g_dumpDir;

    std::filesystem::path EnsureDumpDir() {
        if (!g_dumpDir.empty()) return g_dumpDir;

        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
            std::filesystem::path base(localAppData);
            CoTaskMemFree(localAppData);
            auto dir = base / L"ColonyGame" / L"crashdumps";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return dir;
        }
        // Fallback: temp directory
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::filesystem::path dir(tmp); dir /= L"ColonyGame\\crashdumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::wstring NowStamp() {
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t buf[64];
        swprintf(buf, 64, L"%04u-%02u-%02u_%02u-%02u-%02u",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
        auto dir = EnsureDumpDir();
        auto file = dir / (std::wstring(g_appName) + L"_" + NowStamp() + L".dmp");

        HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;

            // A good default set that includes stack, handles, etc.
            MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
                MiniDumpWithHandleData | MiniDumpScanMemory | MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules | MiniDumpWithDataSegs);

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                              hFile, type, &mei, nullptr, nullptr); // returns BOOL
            CloseHandle(hFile);
        }

        // Informative dialog for non-dev users (optional, harmless in dev)
        MessageBoxW(nullptr,
            L"Colony Game encountered an error and created a crash report.\n\n"
            L"Please send the newest *.dmp file from:\n"
            L"%LOCALAPPDATA%\\ColonyGame\\crashdumps",
            g_appName, MB_OK | MB_ICONERROR);

        return EXCEPTION_EXECUTE_HANDLER;
    }
}

bool InstallCrashHandler(const wchar_t* appDisplayName, const wchar_t* dumpDir) {
    if (appDisplayName && *appDisplayName) g_appName = appDisplayName;
    if (dumpDir && *dumpDir) {
        g_dumpDir = std::filesystem::path(dumpDir);
        std::error_code ec; std::filesystem::create_directories(g_dumpDir, ec);
    }
    SetUnhandledExceptionFilter(&TopLevelFilter); // standard Windows pattern
    return true;
}
