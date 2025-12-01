// Windows-only crash handler (wide paths + clear error logs, no macro redefinitions)
#include "CrashHandler.h"

#include <windows.h>        // include directly; WIN32_LEAN_AND_MEAN is defined by the build
#include <DbgHelp.h>
#include <filesystem>
#include <string>
#include <sstream>
#include <cwchar>

#pragma comment(lib, "Dbghelp.lib")

namespace {
    std::wstring g_dumpDir;

    static std::wstring FormatLastErrorW(DWORD err)
    {
        if (!err) return L"";
        LPWSTR buf = nullptr;
        DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
        std::wstring s;
        if (n && buf) { s.assign(buf, n); ::LocalFree(buf); }
        // Trim trailing CR/LF
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
        return s;
    }

    static void DebugPrintError(const wchar_t* where, DWORD err)
    {
        std::wstringstream ss;
        ss << L"[CrashHandler] " << where << L" failed: " << FormatLastErrorW(err)
           << L" (0x" << std::hex << err << L")\n";
        ::OutputDebugStringW(ss.str().c_str());
    }

    static void DebugPrintInfo(const std::wstring& text)
    {
        std::wstring msg = L"[CrashHandler] ";
        msg += text;
        msg += L"\n";
        ::OutputDebugStringW(msg.c_str());
    }

    static LONG WINAPI ColonyUnhandledExceptionFilter(EXCEPTION_POINTERS* info)
    {
        // Ensure directory exists (supports nested paths)
        std::error_code ec;
        if (!g_dumpDir.empty())
            std::filesystem::create_directories(g_dumpDir, ec);
        if (ec) DebugPrintError(L"create_directories", (DWORD)ec.value());

        // Timestamped filename
        SYSTEMTIME st; ::GetLocalTime(&st);
        wchar_t name[256];
        swprintf_s(name, L"ColonyCrash_%04u%02u%02u_%02u%02u%02u.dmp",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        std::filesystem::path path = std::filesystem::path(g_dumpDir.empty() ? L"." : g_dumpDir) / name;

        HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            DebugPrintError(L"CreateFileW", ::GetLastError());
            return EXCEPTION_EXECUTE_HANDLER;
        }

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = ::GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers    = FALSE;

        // Useful default set; cast to the correct enum type
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory |
            MiniDumpWithUnloadedModules |
            MiniDumpWithProcessThreadData |
            MiniDumpWithHandleData
        );

        BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(),
                                      ::GetCurrentProcessId(),
                                      hFile,
                                      dumpType,
                                      &mei,
                                      nullptr,
                                      nullptr);
        if (!ok) {
            DebugPrintError(L"MiniDumpWriteDump", ::GetLastError());
        } else {
            std::wstringstream ss;
            ss << L"Minidump written: " << path.c_str();
            DebugPrintInfo(ss.str());
        }

        ::FlushFileBuffers(hFile);
        ::CloseHandle(hFile);
        return EXCEPTION_EXECUTE_HANDLER; // Let OS terminate after writing the dump
    }
} // namespace

void CrashHandler::Install(const wchar_t* dumpDir)
{
    g_dumpDir = dumpDir ? dumpDir : L".";
    // Optionally pre-create; errors are non-fatal here
    std::error_code ec;
    std::filesystem::create_directories(g_dumpDir, ec);
    if (ec) DebugPrintError(L"create_directories(Install)", (DWORD)ec.value());

    ::SetUnhandledExceptionFilter(&ColonyUnhandledExceptionFilter);
    DebugPrintInfo(L"Unhandled exception filter installed");
}
