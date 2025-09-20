// CrashHandler.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

namespace fs = std::filesystem;

static fs::path g_dumpDir;

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* pEx) {
    // timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::wstringstream name;
    name << L"crash-" << (1900 + tm.tm_year)
         << L"-" << std::setw(2) << std::setfill(L'0') << (tm.tm_mon + 1)
         << L"-" << std::setw(2) << tm.tm_mday
         << L"_" << std::setw(2) << tm.tm_hour
         << L"-" << std::setw(2) << tm.tm_min
         << L"-" << std::setw(2) << tm.tm_sec
         << L".dmp";

    fs::path outPath = g_dumpDir / name.str();

    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pEx;
    mei.ClientPointers = FALSE;

    // A balanced minidump type that keeps dumps small but useful:
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
        MiniDumpWithThreadInfo |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory
    );

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mei, nullptr, nullptr);
    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
}

namespace app::crash {
    void install_minidump_handler(const fs::path& dumpDir) {
        g_dumpDir = dumpDir;
        SetUnhandledExceptionFilter(TopLevelFilter); // hooks unhandled exceptions
    }
}
