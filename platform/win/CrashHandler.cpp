#define WIN32_LEAN_AND_MEAN
#include "platform/win/CrashHandler.h"
#include "core/Log.h"
#include <windows.h>
#include <dbghelp.h>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "dbghelp.lib")

namespace cg {

static std::filesystem::path g_dumpDir;

static std::wstring WNowStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    std::error_code ec;
    std::filesystem::create_directories(g_dumpDir, ec);

    std::wstring fname = L"crash_" + WNowStamp() + L".dmp";
    auto full = g_dumpDir / fname;

    HANDLE hFile = CreateFileW(full.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(hFile);

        cg::Log::Error("Crash dump written to: " + std::string(full.string()));
    } else {
        cg::Log::Error("Failed to create crash dump file.");
    }

    MessageBoxW(nullptr,
        L"Colony-Game encountered a fatal error.\n"
        L"A crash report (.dmp) was saved in the 'crashdumps' folder next to the .exe.",
        L"Colony-Game", MB_ICONERROR | MB_OK);

    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler(const std::filesystem::path& dumpDir) {
    g_dumpDir = dumpDir;
    SetErrorMode(SEM_FAILCRITICALERRORS);
    SetUnhandledExceptionFilter(TopLevelFilter);
    cg::Log::Info("Crash handler installed.");
}

} // namespace cg
