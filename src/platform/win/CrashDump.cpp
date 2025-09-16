#include "platform/win/WinHeaders.hpp"
#include <dbghelp.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace fs = std::filesystem;

namespace {
    std::wstring g_dumpDir;
    std::wstring g_appTag;

    std::wstring Timestamp() {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[64]{};
        swprintf_s(buf, L"%04u-%02u-%02u_%02u%02u%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
        if (g_dumpDir.empty()) return EXCEPTION_EXECUTE_HANDLER;

        // Ensure directory exists
        fs::create_directories(fs::path(g_dumpDir));

        // Build dump path
        fs::path dumpPath = fs::path(g_dumpDir) /
            (g_appTag + L"_" + Timestamp() + L".dmp");

        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_EXECUTE_HANDLER;

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);

        return EXCEPTION_EXECUTE_HANDLER;
    }
}

namespace cg::win {
    bool InstallCrashHandler(const std::wstring& dumpDir, const std::wstring& appTag) {
        g_dumpDir = dumpDir;
        g_appTag  = appTag;
        SetUnhandledExceptionFilter(TopLevelFilter);
        return true;
    }
}
