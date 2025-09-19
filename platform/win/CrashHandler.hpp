#pragma once
// Windows-only crash handler + bootstrap for Colony-Game.
// Header-only: include from WinLauncher.cpp and call cg::win::CrashHandler::Install(L"ColonyGame");
// Requires MSVC or any compiler that supports <windows.h> and DbgHelp.

#if !defined(_WIN32)
#error "CrashHandler.hpp is Windows-only"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>

#pragma comment(lib, "Dbghelp.lib")

namespace cg::win {

class CrashHandler {
public:
    // Install crash handler and normalize startup state.
    // - appName is used to name dump files.
    // - If fixWorkingDir is true, sets CWD to the EXE directory (prevents asset path issues).
    static void Install(const wchar_t* appName, bool fixWorkingDir = true, bool showMessageBox = true) {
        s_showMessageBox = showMessageBox;
        s_appName = (appName ? std::wstring(appName) : L"App");
        ::SetErrorMode(::GetErrorMode() | SEM_NOGPFAULTERRORBOX); // Avoid OS crash dialog
        ::SetUnhandledExceptionFilter(&CrashHandler::TopLevelFilter);

        // Optional: Ensure CWD is the EXE folder to avoid "launch errors" due to relative paths.
        if (fixWorkingDir) {
            if (auto dir = executableDir(); !dir.empty()) {
                ::SetCurrentDirectoryW(dir.c_str());
            }
        }
    }

    // If you catch a std::exception at top level and still want a breadcrumb:
    static void LogUnhandledStdException(const std::exception& e) {
        try {
            auto logDir = ensureDir(L"logs");
            auto log = logDir / L"last-std-exception.txt";
            std::wofstream f(log, std::ios::out | std::ios::trunc);
            f.imbue(std::locale("C"));
            f << L"std::exception.what(): ";
            // Write as wide chars best-effort:
            std::wstring wmsg; wmsg.reserve(256);
            for (const auto ch : std::string(e.what())) wmsg.push_back(static_cast<unsigned char>(ch));
            f << wmsg << L"\n";
        } catch (...) {
            // no-throw
        }
    }

    // Helper to generate a test dump at will:
    [[noreturn]] static void ForceCrashDumpForTesting() {
        // Deliberately raise an SEH exception so our filter runs.
        ::RaiseException(0xE0000001, 0, 0, nullptr);
        std::abort(); // never reached
    }

private:
    static inline std::mutex s_mtx{};
    static inline std::wstring s_appName = L"App";
    static inline bool s_showMessageBox = true;

    static std::filesystem::path executableDir() {
        wchar_t buf[MAX_PATH];
        DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) { return {}; }
        return std::filesystem::path(buf).parent_path();
    }

    static std::filesystem::path ensureDir(const wchar_t* name) {
        auto base = executableDir();
        auto dir = base / name;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    static std::wstring nowStamp() {
        SYSTEMTIME st; ::GetLocalTime(&st);
        std::wstringstream ss;
        ss << std::setfill(L'0')
           << st.wYear  << L"-"
           << std::setw(2) << st.wMonth  << L"-"
           << std::setw(2) << st.wDay    << L"_"
           << std::setw(2) << st.wHour   << L"-"
           << std::setw(2) << st.wMinute << L"-"
           << std::setw(2) << st.wSecond;
        return ss.str();
    }

    static std::filesystem::path nextDumpPath() {
        auto dumps = ensureDir(L"crashdumps");
        std::wstringstream name;
        name << s_appName << L"_" << nowStamp() << L"_pid" << ::GetCurrentProcessId() << L".dmp";
        return dumps / name.str();
    }

    static bool writeMiniDump(EXCEPTION_POINTERS* info, std::filesystem::path& outPath) {
        std::scoped_lock lk(s_mtx);
        outPath = nextDumpPath();

        HANDLE hFile = ::CreateFileW(
            outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION exInfo{};
        exInfo.ThreadId = ::GetCurrentThreadId();
        exInfo.ExceptionPointers = info;
        exInfo.ClientPointers = FALSE;

        MINIDUMP_TYPE dumpType =
            (MINIDUMP_TYPE)(MiniDumpWithDataSegs |
                            MiniDumpWithHandleData |
                            MiniDumpWithThreadInfo |
                            MiniDumpWithUnloadedModules);
#if defined(_DEBUG)
        dumpType = (MINIDUMP_TYPE)(dumpType |
                                   MiniDumpWithFullMemory |
                                   MiniDumpWithFullMemoryInfo |
                                   MiniDumpWithProcessThreadData);
#endif

        BOOL ok = ::MiniDumpWriteDump(
            ::GetCurrentProcess(), ::GetCurrentProcessId(), hFile,
            dumpType, &exInfo, nullptr, nullptr);

        ::CloseHandle(hFile);
        return ok == TRUE;
    }

    static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* info) {
        std::filesystem::path dumpPath;
        bool ok = writeMiniDump(info, dumpPath);

        if (s_showMessageBox) {
            std::wstringstream msg;
            if (ok) {
                msg << L"A fatal error occurred and a crash report was saved:\n\n"
                    << dumpPath.c_str()
                    << L"\n\nPlease send this file so we can fix the issue.";
            } else {
                msg << L"A fatal error occurred.\n\n"
                    << L"(Crash dump could not be written.)";
            }
            ::MessageBoxW(nullptr, msg.str().c_str(), s_appName.c_str(),
                          MB_OK | MB_ICONERROR | MB_TASKMODAL);
        }

        // Let the process terminate.
        return EXCEPTION_EXECUTE_HANDLER;
    }
};

} // namespace cg::win
