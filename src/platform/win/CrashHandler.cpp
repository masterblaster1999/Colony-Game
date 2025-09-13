#ifdef _WIN32
#include "CrashHandler.h"
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

namespace fs = std::filesystem;

namespace {
    std::wstring g_appName;

    std::wstring Timestamp() {
        using clock = std::chrono::system_clock;
        auto t  = clock::to_time_t(clock::now());
        std::tm tm{};
        localtime_s(&tm, &t);
        std::wstringstream ss;
        ss << std::put_time(&tm, L"%Y%m%d_%H%M%S");
        return ss.str();
    }

    std::wstring ExeDir() {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        fs::path p(path);
        return p.remove_filename().wstring();
    }

    std::wstring DumpDir() {
        fs::path dir = fs::path(ExeDir()) / L"CrashDumps";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir.wstring();
    }

    LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* pExceptionInfo) {
        // Write minidump near the exe so users can send it back easily.
        fs::path dumpPath = fs::path(DumpDir())
                          / (g_appName + L"_" + Timestamp() + L".dmp");

        HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei;
            mdei.ThreadId           = GetCurrentThreadId();
            mdei.ExceptionPointers  = pExceptionInfo;
            mdei.ClientPointers     = FALSE;

            MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
                                                MiniDumpScanMemory |
                                                MiniDumpWithThreadInfo |
                                                MiniDumpWithDataSegs);

            ::MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                hFile, mdt, &mdei, nullptr, nullptr);
            ::CloseHandle(hFile);
        }

        // Let Windows Error Reporting also do its thing (or set SEM_NOGPFAULTERRORBOX to suppress).
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

namespace winboot {

void InstallCrashHandler(const std::wstring& appName) {
    g_appName = appName;
    ::SetUnhandledExceptionFilter(TopLevelFilter);

    // Optional: make crashes not show the OS dialog for end users:
    // ::SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
}

} // namespace winboot
#endif
