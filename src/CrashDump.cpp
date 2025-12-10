#include "CrashDump.h"
#include <DbgHelp.h>
#include <cstdio>
#include <string>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

namespace {
    std::wstring g_appName = L"ColonyGame";

    std::wstring NowStamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[64]{};
        swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    std::filesystem::path CrashDir()
    {
        std::wstring localAppData;
        wchar_t* p = nullptr;
        size_t len = 0;
        _wdupenv_s(&p, &len, L"LOCALAPPDATA");
        if (p) { localAppData.assign(p); free(p); }
        if (localAppData.empty()) localAppData = L".";
        auto dir = std::filesystem::path(localAppData) / g_appName / L"CrashDumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    LONG WINAPI DumpUnhandledException(_In_ EXCEPTION_POINTERS* ep)
    {
        auto dumpPath = CrashDir() / (g_appName + L"_" + NowStamp() + L"_" + std::to_wstring(GetCurrentProcessId()) + L".dmp");
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;

            // Light dump is usually sufficient; switch to MiniDumpWithFullMemory if you need it
            MINIDUMP_TYPE mdt = MiniDumpNormal;

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, mdt, ep ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(hFile);
        }
        // Let Windows error UI/wer handle aftermath; return EXCEPTION_EXECUTE_HANDLER if you prefer to exit silently
        return EXCEPTION_CONTINUE_SEARCH;
    }
} // anon

void wincrash::InitCrashHandler(const wchar_t* appName)
{
    if (appName && *appName) g_appName = appName;
    // Suppress legacy GP fault dialog
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    // Register handler (see MS guidance for pros/cons of in-proc minidumping)
    SetUnhandledExceptionFilter(DumpUnhandledException); // :contentReference[oaicite:5]{index=5}
}
