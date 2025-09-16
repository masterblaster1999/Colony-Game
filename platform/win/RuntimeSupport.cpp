#include "RuntimeSupport.h"
#include <dbghelp.h>
#include <shlwapi.h>
#include <strsafe.h>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {
    std::wstring g_dumpDir;
    HANDLE       g_singleInstanceMutex = nullptr;

    std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
        std::wstring p = a;
        if (!p.empty() && p.back() != L'\\') p.push_back(L'\\');
        p += b;
        return p;
    }

    std::wstring NowTimestamp() {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t buf[64];
        StringCchPrintfW(buf, 64, L"%04u%02u%02u_%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* pExc) {
        // Compose dump path
        std::wstring file = JoinPath(g_dumpDir,
            L"colony_crash_" + NowTimestamp() + L"_" + std::to_wstring(GetCurrentProcessId()) + L".dmp");

        // Ensure directory exists
        CreateDirectoryW(g_dumpDir.c_str(), nullptr);

        HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei;
            mdei.ThreadId          = GetCurrentThreadId();
            mdei.ExceptionPointers = pExc;
            mdei.ClientPointers    = FALSE;

            // Reasonable default with threads & handles; tweak as needed.
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                              (MINIDUMP_TYPE)(MiniDumpWithDataSegs |
                                              MiniDumpWithThreadInfo |
                                              MiniDumpWithHandleData),
                              &mdei, nullptr, nullptr);
            CloseHandle(hFile);

            wchar_t msg[1024];
            StringCchPrintfW(msg, 1024,
                L"Sorry, the game crashed.\n\nA crash dump was written to:\n%s\n\n"
                L"Please attach this file when reporting the issue.", file.c_str());
            MessageBoxW(nullptr, msg, L"Colony Game - Crash", MB_OK | MB_ICONERROR);
        }
        return EXCEPTION_EXECUTE_HANDLER; // let OS terminate after writing dump
    }
}

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, buf + len);
    PathRemoveFileSpecW(buf);
    return std::wstring(buf);
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

void FixWorkingDirectoryToExe() {
    std::wstring exeDir = GetExeDir();
    SetCurrentDirectoryW(exeDir.c_str());
}

bool EnsureSingleInstance(const wchar_t* mutexName) {
    g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, mutexName);
    if (!g_singleInstanceMutex) return true; // best effort
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Colony Game is already running.", L"Colony Game", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

void SetPerMonitorDpiAware() {
    // Prefer manifest in production, but enable at runtime for dev builds too.
    // Dynamically resolve to keep compatibility on older Windows.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    typedef BOOL (WINAPI *SetProcDpiCtx)(HANDLE);
    auto pSet = reinterpret_cast<SetProcDpiCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    // PER_MONITOR_AWARE_V2 constant value is (HANDLE)-4
    #ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
    #endif
    if (pSet) pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

bool InitCrashHandler(const wchar_t* dumpSubdir) {
    g_dumpDir = JoinPath(GetExeDir(), dumpSubdir);
    SetUnhandledExceptionFilter(&TopLevelExceptionFilter);
    return true;
}
