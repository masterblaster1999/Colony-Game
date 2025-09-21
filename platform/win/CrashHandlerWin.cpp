// CrashHandlerWin.cpp
// Windows-only unhandled-exception minidump writer for Colony-Game.
// Creates a .dmp in %LOCALAPPDATA%\ColonyGame\logs and shows a friendly dialog.
//
//  - Installs with SetUnhandledExceptionFilter (process-wide).
//  - Loads dbghelp.dll from System32 (safe search order) and calls MiniDumpWriteDump.
//  - Keeps this file outside colony_core; compile it into the Windows front-end.
//
// References:
//   SetUnhandledExceptionFilter: https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter
//   MiniDumpWriteDump:           https://learn.microsoft.com/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump
//   DLL search order guidance:   https://learn.microsoft.com/windows/win32/dlls/dynamic-link-library-search-order
//
// Notes:
//   - When a debugger is attached, Windows typically invokes the debugger instead of your filter.
//     That’s expected (see Raymond Chen/Old New Thing). Run without a debugger to exercise the filter.
//   - The filter is process-wide; install from the .exe (not a DLL) to avoid impolite overrides.
//
// Build: MSVC / Windows SDK; no import lib for DbgHelp is required (we LoadLibrary at runtime).

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <DbgHelp.h>            // Types only; MiniDumpWriteDump is called via function pointer
#include <ShlObj.h>             // (headers commonly available in the SDK)
#include <filesystem>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cwchar>

#include "PathUtilWin.h"        // winpath::{writable_data_dir(), exe_dir()}

namespace fs = std::filesystem;

// ---- Safe dbghelp loader ----------------------------------------------------

using MiniDumpWriteDump_t = BOOL (WINAPI*)(
    HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    CONST PMINIDUMP_EXCEPTION_INFORMATION,
    CONST PMINIDUMP_USER_STREAM_INFORMATION,
    CONST PMINIDUMP_CALLBACK_INFORMATION);

// Load dbghelp.dll from System32 if available. Fall back to explicit
// <SystemDirectory>\dbghelp.dll path for older systems.
static HMODULE LoadDbgHelpFromSystem()
{
    HMODULE h = nullptr;

    // Prefer LoadLibraryExW with LOAD_LIBRARY_SEARCH_SYSTEM32 when available.
    auto pLoadLibraryExW = ::LoadLibraryExW;
#ifdef LOAD_LIBRARY_SEARCH_SYSTEM32
    if (pLoadLibraryExW) {
        h = pLoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) return h;
    }
#endif

    // Fallback: build an explicit System32 path and load by full path.
    wchar_t sysdir[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring full = std::wstring(sysdir) + L"\\dbghelp.dll";
        h = ::LoadLibraryW(full.c_str());
        if (h) return h;
    }

    // Last resort: regular search (not ideal, but better than nothing).
    return ::LoadLibraryW(L"dbghelp.dll");
}

// ---- Utilities --------------------------------------------------------------

static std::wstring LastErrorMessage(DWORD err = GetLastError())
{
    LPWSTR msg = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, nullptr);
    std::wstring out = (len && msg) ? msg : L"";
    if (msg) LocalFree(msg);
    return out;
}

static fs::path LogsDir()
{
    fs::path out = winpath::writable_data_dir() / L"logs";
    std::error_code ec;
    fs::create_directories(out, ec);
    return out;
}

static std::wstring NowStamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::wstringstream ss;
    ss << std::put_time(&tm, L"%Y%m%d-%H%M%S");
    return ss.str();
}

static void FriendlyBox(const std::wstring& dumpPath)
{
    const fs::path logs = LogsDir();
    std::wstring msg =
        L"Colony-Game encountered an unexpected error and needs to close.\n\n"
        L"A crash report (minidump) was saved to:\n\n    " + dumpPath +
        L"\n\nLogs are here:\n\n    " + logs.wstring() +
        L"\n\nYou can share these files with the developer for debugging. Thanks!";

    MessageBoxW(nullptr, msg.c_str(), L"Colony-Game crashed", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

// Choose a sensible default dump type (good balance of size & utility).
// You can switch to full memory via environment variable CG_DUMP=full.
static MINIDUMP_TYPE ChooseDumpType()
{
    wchar_t buf[16]{};
    DWORD got = GetEnvironmentVariableW(L"CG_DUMP", buf, 16);
    bool full = (got && (_wcsicmp(buf, L"full") == 0));

    if (full)
        return (MINIDUMP_TYPE)(
            MiniDumpWithFullMemory |
            MiniDumpWithHandleData |
            MiniDumpWithThreadInfo |
            MiniDumpWithUnloadedModules |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithThreadInfo);

    return (MINIDUMP_TYPE)(
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithDataSegs |
        MiniDumpWithPrivateReadWriteMemory |
        MiniDumpWithIndirectlyReferencedMemory);
}

// ---- The actual unhandled-exception filter ---------------------------------

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    // Build output path: %LOCALAPPDATA%\ColonyGame\logs\crash-YYYYMMDD-HHMMSS-PID.dmp
    const DWORD pid = GetCurrentProcessId();
    const std::wstring stamp = NowStamp();

    fs::path outDir = LogsDir();
    fs::path outPath = outDir / (L"crash-" + stamp + L"-" + std::to_wstring(pid) + L".dmp");

    // Open destination file.
    HANDLE hFile = CreateFileW(outPath.c_str(),
                               GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        FriendlyBox(L"(failed to create dump file; see logs)");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Load dbghelp and resolve MiniDumpWriteDump.
    HMODULE hDbgHelp = LoadDbgHelpFromSystem();
    if (!hDbgHelp) {
        CloseHandle(hFile);
        FriendlyBox(L"(failed to load dbghelp.dll; see logs)");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    auto pMiniDumpWriteDump = reinterpret_cast<MiniDumpWriteDump_t>(
        GetProcAddress(hDbgHelp, "MiniDumpWriteDump"));
    if (!pMiniDumpWriteDump) {
        FreeLibrary(hDbgHelp);
        CloseHandle(hFile);
        FriendlyBox(L"(MiniDumpWriteDump not found in dbghelp.dll)");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Prepare dump parameters.
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    MINIDUMP_TYPE dumpType = ChooseDumpType();

    // Write the dump.
    BOOL ok = pMiniDumpWriteDump(
        GetCurrentProcess(),
        pid,
        hFile,
        dumpType,
        ep ? &mei : nullptr,
        nullptr,     // user streams
        nullptr);    // callback (could be used to filter modules, etc.)

    DWORD writeErr = ok ? ERROR_SUCCESS : GetLastError();

    CloseHandle(hFile);
    FreeLibrary(hDbgHelp);

    if (!ok) {
        std::wstring msg = L"(failed to write dump: error " +
                           std::to_wstring(writeErr) + L" - " + LastErrorMessage(writeErr) + L")";
        FriendlyBox(msg);
    } else {
        FriendlyBox(outPath.wstring());
    }

    // Let the process terminate after we’ve done our work.
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---- Public API -------------------------------------------------------------
//
// Call CrashHandler_Install() once at startup (e.g., top of wWinMain in WinLauncher.cpp).
// If you want to temporarily disable it (e.g., inside a tool), you can restore the previous filter.

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

extern "C" void CrashHandler_Install()
{
    // Install process-wide unhandled exception filter.
    g_prevFilter = SetUnhandledExceptionFilter(&CrashFilter);
}

extern "C" void CrashHandler_Uninstall()
{
    SetUnhandledExceptionFilter(g_prevFilter);
    g_prevFilter = nullptr;
}

// Optional: auto-install when this TU is linked into the .exe.
// #define CG_CRASH_AUTO_INSTALL 1
#ifdef CG_CRASH_AUTO_INSTALL
namespace { struct Auto { Auto(){ CrashHandler_Install(); } } g_auto; }
#endif
