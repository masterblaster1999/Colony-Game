// platform/win/CrashHandler.cpp
#include "CrashHandler.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <string>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

// --------------------------------------------------------------------------------------
// Helpers: error formatting + debug print
// --------------------------------------------------------------------------------------
static std::wstring FormatLastError(DWORD err)
{
    LPWSTR buf = nullptr;
    DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                               FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s;
    if (n && buf) { s.assign(buf, n); ::LocalFree(buf); }
    else {
        wchar_t tmp[64];
        swprintf_s(tmp, L"Unknown error (0x%08X)", err);
        s = tmp;
    }
    // Trim trailing CR/LF if present
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
    return s;
}

static void DebugPrintError(const wchar_t* where, DWORD err)
{
    std::wstring msg = L"[CrashHandler] ";
    msg += where;
    msg += L" failed: ";
    msg += FormatLastError(err);
    wchar_t hex[20];
    swprintf_s(hex, L" (0x%08X)\n", err);
    msg += hex;
    ::OutputDebugStringW(msg.c_str());
}

static void DebugPrintInfo(const std::wstring& text)
{
    std::wstring msg = L"[CrashHandler] ";
    msg += text;
    msg += L"\n";
    ::OutputDebugStringW(msg.c_str());
}

// --------------------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------------------
static std::wstring g_dumpDir;

// --------------------------------------------------------------------------------------
// Unhandled exception filter (writes a minidump and returns EXECUTE_HANDLER)
// --------------------------------------------------------------------------------------
static LONG WINAPI ColonyUnhandledExceptionFilter(EXCEPTION_POINTERS* info)
{
    // 1) Ensure dump directory exists (supports nested paths).
    std::error_code ec;
    std::filesystem::create_directories(g_dumpDir, ec);
    if (ec) {
        DebugPrintError(L"create_directories", (DWORD)ec.value());
    }

    // 2) Build timestamped file name (UTC isn't required here; local time is fine for triage).
    SYSTEMTIME st; ::GetLocalTime(&st);
    wchar_t name[256];
    swprintf_s(name, L"ColonyCrash_%04u%02u%02u_%02u%02u%02u.dmp",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::filesystem::path path = std::filesystem::path(g_dumpDir) / name;

    // 3) Create dump file
    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugPrintError(L"CreateFileW", ::GetLastError());
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // 4) Fill exception info for MiniDumpWriteDump
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;

    // 5) Choose a useful set of flags (cast to MINIDUMP_TYPE to satisfy the API)
    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory |
        MiniDumpWithThreadInfo |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules
        // For very deep investigations, consider adding MiniDumpWithFullMemory (large files).
    );

    // 6) Write the dump and log status
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
    return EXCEPTION_EXECUTE_HANDLER; // let OS terminate
}

// --------------------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------------------
void CrashHandler::Install(const wchar_t* dumpDir)
{
    // Wide path input retained; default remains current directory for backwards compatibility.
    // (If you want a safer default, pass e.g. L"%LOCALAPPDATA%\\ColonyGame\\Crashes" expanded by your platform layer.)
    g_dumpDir = dumpDir ? dumpDir : L".";

    // Optionally pre-create the directory here (we also create on crash).
    std::error_code ec;
    std::filesystem::create_directories(g_dumpDir, ec);
    if (ec) {
        DebugPrintError(L"create_directories (Install)", (DWORD)ec.value());
    }

    ::SetUnhandledExceptionFilter(&ColonyUnhandledExceptionFilter);
    DebugPrintInfo(L"Unhandled exception filter installed");
}
