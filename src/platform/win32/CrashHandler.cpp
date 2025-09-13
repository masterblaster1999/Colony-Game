#include "CrashHandler.h"
#include "AppPaths.h"
#include <windows.h>
#include <dbghelp.h>      // MiniDumpWriteDump
#include <crtdbg.h>       // _set_invalid_parameter_handler
#include <mutex>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Shell32.lib")

namespace {
    std::wofstream g_log;
    std::mutex     g_lock;
    std::wstring   g_appName;
    std::wstring   g_appVersion;
    std::wstring   g_dumpDir;

    std::wstring timestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);
        std::tm tm_{}; localtime_s(&tm_, &t);
        wchar_t buf[32];
        wcsftime(buf, 32, L"%Y%m%d-%H%M%S", &tm_);
        return buf;
    }

    void writeDump(EXCEPTION_POINTERS* ep) {
        std::wstring file = g_dumpDir + L"\\" + g_appName +
                            L"_" + timestamp() + L".dmp";
        HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;

        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, MiniDumpWithIndirectlyReferencedMemory,
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }

    LONG WINAPI unhandled(EXCEPTION_POINTERS* ep) {
        {
            std::lock_guard<std::mutex> _{g_lock};
            if (g_log.is_open()) {
                g_log << L"[CRASH] Unhandled exception. Writing minidump...\n";
                g_log.flush();
            }
        }
        writeDump(ep); // See docs; in-process is acceptable for many games. :contentReference[oaicite:9]{index=9}
        MessageBoxW(nullptr,
            L"Colony-Game encountered a fatal error.\n\n"
            L"A crash report was written to your %LOCALAPPDATA%\\ColonyGame\\dumps folder.\n"
            L"Please attach the .dmp and the latest log from %LOCALAPPDATA%\\ColonyGame\\logs when reporting.",
            L"Colony-Game Crash", MB_ICONERROR | MB_OK);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void __cdecl invalid_param_handler(const wchar_t*, const wchar_t*,
                                       const wchar_t*, unsigned, uintptr_t) {
        winqol::LogLine(L"[CRT] Invalid parameter handler fired.");
        // Let normal flow continue; unhandled filter will catch later if needed.
    }

    void __cdecl purecall_handler() {
        winqol::LogLine(L"[CRT] Pure virtual function call.");
    }
}

namespace winqol {
    void InstallCrashHandler(const std::wstring& appName,
                             const std::wstring& appVersion) {
        g_appName    = appName;
        g_appVersion = appVersion;
        auto logs    = LogsDir(appName);
        g_dumpDir    = DumpsDir(appName);

        // Open a timestamped log file (UTF-16 for simplicity).
        std::wstring logPath = logs + L"\\" + appName + L"_" + timestamp() + L".log";
        g_log.open(logPath, std::ios::out | std::ios::trunc);
        if (g_log.is_open()) {
            g_log << L"[BOOT] " << appName << L" v" << appVersion << L"\n";
            g_log << L"[BOOT] exe dir: " << ExeDir() << L"\n";
        }

        // Install handlers very early.
        SetUnhandledExceptionFilter(&unhandled);             // top-level SEH hook :contentReference[oaicite:10]{index=10}
        _set_invalid_parameter_handler(invalid_param_handler);
        _set_purecall_handler(purecall_handler);
    }

    void UninstallCrashHandler() {
        std::lock_guard<std::mutex> _{g_lock};
        if (g_log.is_open()) g_log.flush();
        g_log.close();
    }

    void LogLine(const std::wstring& line) {
        std::lock_guard<std::mutex> _{g_lock};
        if (g_log.is_open()) { g_log << line << L"\n"; g_log.flush(); }
        OutputDebugStringW((line + L"\n").c_str());
    }
}
