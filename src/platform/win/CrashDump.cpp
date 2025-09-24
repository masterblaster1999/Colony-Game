// src/platform/win/CrashDump.cpp
//
// Windows-only unhandled-exception minidump writer for Colony-Game.
// - Installs a top-level unhandled exception filter.
// - Writes timestamped .dmp files into a caller-specified directory.
// - Includes extra diagnostic streams (thread info, unloaded modules, memory info, comment).
//
// Integration:
//   1) Call cg::win::InstallCrashHandler(L"<crash-dir>", L"ColonyGame") very early in app init.
//   2) Ensure your link flags include Dbghelp.lib (this TU adds a pragma for MSVC).
//
// Notes:
//   * MiniDumpWriteDump is commonly invoked in-process on crash. For maximum robustness,
//     Microsoft recommends dumping from a helper process, but this in-process path is
//     sufficient for most desktop games and avoids extra complexity.
//
// Build: Windows, C++17

#include "platform/win/WinHeaders.hpp"
#include <dbghelp.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace fs = std::filesystem;

namespace
{
    std::wstring g_dumpDir;
    std::wstring g_appTag;

    // Format: YYYY-MM-DD_HHMMSS
    std::wstring Timestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[64]{};
        swprintf_s(buf, L"%04u-%02u-%02u_%02u%02u%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    // Compose a friendly comment to embed into the minidump.
    std::wstring BuildComment()
    {
        std::wstring c;
        c.reserve(256);
        c += L"App: ";
        c += g_appTag.empty() ? L"ColonyGame" : g_appTag;
        c += L"\n";
        c += L"Timestamp: ";
        c += Timestamp();
        c += L"\n";
        return c;
    }

    LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep)
    {
        if (g_dumpDir.empty())
            return EXCEPTION_EXECUTE_HANDLER;

        // Ensure directory exists (ignore failures; CreateFileW will fail later if invalid).
        std::error_code ec;
        fs::create_directories(fs::path(g_dumpDir), ec);

        // Build dump path: <dir>/<appTag>_YYYY-MM-DD_HHMMSS.dmp
        const std::wstring app = g_appTag.empty() ? L"ColonyGame" : g_appTag;
        fs::path dumpPath = fs::path(g_dumpDir) / (app + L"_" + Timestamp() + L".dmp");

        HANDLE hFile = CreateFileW(dumpPath.c_str(),
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ,
                                   nullptr,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return EXCEPTION_EXECUTE_HANDLER;

        // Enrich the dump with extra metadata and streams where available.
        MINIDUMP_TYPE dumpType =
            (MINIDUMP_TYPE)(
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory |
                MiniDumpWithProcessThreadData |
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithFullMemoryInfo
            );

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        // Add a UTF-16 comment stream (helpful to tag build info, timestamps, etc.)
        const std::wstring comment = BuildComment();
        MINIDUMP_USER_STREAM commentStream{};
        commentStream.Type = (MINIDUMP_STREAM_TYPE)CommentStreamW;
        commentStream.Buffer = (void*)comment.c_str();
        commentStream.BufferSize = static_cast<ULONG>((comment.size() + 1) * sizeof(wchar_t));

        MINIDUMP_USER_STREAM_INFORMATION userInfo{};
        userInfo.UserStreamCount = 1;
        userInfo.UserStreamArray = &commentStream;

        // Write the dump
        MiniDumpWriteDump(GetCurrentProcess(),
                          GetCurrentProcessId(),
                          hFile,
                          dumpType,
                          ep ? &mei : nullptr,
                          &userInfo,
                          nullptr);

        CloseHandle(hFile);

        // Continue with default post-crash behavior (let the process terminate).
        return EXCEPTION_EXECUTE_HANDLER;
    }
} // anonymous namespace

namespace cg::win
{
    // Install the crash handler. If dumpDir is empty, nothing is installed.
    // Recommended dumpDir example: L"./crash" or a path under %LOCALAPPDATA%.
    bool InstallCrashHandler(const std::wstring& dumpDir, const std::wstring& appTag)
    {
        g_dumpDir = dumpDir;
        g_appTag  = appTag;
        SetUnhandledExceptionFilter(TopLevelFilter);
        return true;
    }
}
