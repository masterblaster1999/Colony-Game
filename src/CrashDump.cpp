#include "CrashDump.h"
#include <DbgHelp.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <string>
#include <filesystem>

#include "platform/win/PathUtilWin.h"

#pragma comment(lib, "Dbghelp.lib")

namespace {
    std::wstring g_appName = L"ColonyGame";
    std::filesystem::path g_dumpDir;

    std::wstring NowStamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[64]{};
        swprintf_s(buf, L"%04u%02u%02u_%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    LONG WINAPI DumpUnhandledException(_In_ EXCEPTION_POINTERS* ep)
    {
        const std::filesystem::path baseDir = g_dumpDir.empty() ? std::filesystem::current_path() : g_dumpDir;
        auto dumpPath = baseDir / (g_appName + L"_" + NowStamp() + L"_" + std::to_wstring(GetCurrentProcessId()) + L".dmp");
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;

            // A “medium” dump that is still reasonably sized but far more useful than MiniDumpNormal.
            // (Full-memory dumps get huge fast and are usually not necessary for first-pass triage.)
            const MINIDUMP_TYPE mdt = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory |
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithProcessThreadData |
                MiniDumpWithDataSegs
            );

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

    // Pick a deterministic dump folder under LocalAppData so users can find it easily.
    // This is computed up-front to avoid doing folder discovery in the crash path.
    winpath::ensure_dirs();
    g_dumpDir = winpath::crashdump_dir();

    // Suppress legacy GP fault dialog
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    // Register handler (see MS guidance for pros/cons of in-proc minidumping)
    SetUnhandledExceptionFilter(DumpUnhandledException);
}
