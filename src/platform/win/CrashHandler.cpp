#ifdef _WIN32
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "CrashHandler.h"
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <atomic>
#include <cstdio>

#pragma comment(lib, "Dbghelp.lib")

// -------------------------
// Compile-time defaults
// -------------------------
// Prefer %LOCALAPPDATA%\<App>\crashdumps; if that fails, fall back to "<exe>\CrashDumps"
#ifndef WINBOOT_USE_LOCALAPPDATA
#  define WINBOOT_USE_LOCALAPPDATA 1
#endif
// Keep at most this many .dmp files (oldest are deleted on install)
#ifndef WINBOOT_MAX_DUMPS
#  define WINBOOT_MAX_DUMPS 25
#endif
// 0 = Minidump (small), 1 = Full memory dump (very large)
#ifndef WINBOOT_FULL_DUMP
#  define WINBOOT_FULL_DUMP 0
#endif
// 1 = suppress Windows crash dialogs for better UX / CI runs
#ifndef WINBOOT_SUPPRESS_WER
#  define WINBOOT_SUPPRESS_WER 1
#endif

namespace fs = std::filesystem;

namespace {

    // ---- State ----
    std::wstring g_appName = L"ColonyGame";
    std::wstring g_dumpDir;
    std::atomic<bool> g_installed{false};
    std::atomic<bool> g_inHandler{false};
    LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

    SRWLOCK g_noteLock = SRWLOCK_INIT;
    std::wstring g_extraNote;

    // ---- Small helpers ----
    std::wstring Timestamp() {
        using clock = std::chrono::system_clock;
        const auto now  = clock::now();
        const auto t    = clock::to_time_t(now);
        std::tm tm{}; localtime_s(&tm, &t);
        // add milliseconds (optional)
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count() % 1000;
        std::wstringstream ss;
        ss << std::put_time(&tm, L"%Y%m%d_%H%M%S") << L'_' << std::setw(3) << std::setfill(L'0') << ms;
        return ss.str();
    }

    std::wstring ExeDir() {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        fs::path p(path);
        return p.remove_filename().wstring();
    }

    std::wstring LocalAppData() {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) return std::wstring(buf, buf + n);
        wchar_t tmp[MAX_PATH]{};
        DWORD m = GetTempPathW(MAX_PATH, tmp);
        return std::wstring(tmp, tmp + m);
    }

    std::wstring ComputeDumpDir() {
        // 1) Environment override
        wchar_t custom[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"COLONY_CRASHDIR", custom, MAX_PATH) && custom[0]) {
            std::error_code ec; fs::create_directories(custom, ec);
            return std::wstring(custom);
        }
        // 2) Preferred LocalAppData\ColonyGame\crashdumps
#if WINBOOT_USE_LOCALAPPDATA
        try {
            fs::path dir = fs::path(LocalAppData()) / g_appName / L"crashdumps";
            std::error_code ec; fs::create_directories(dir, ec);
            if (!ec) return dir.wstring();
        } catch (...) { /* fall through */ }
#endif
        // 3) Fallback: <exe>\CrashDumps (preserves your original layout)
        fs::path dir = fs::path(ExeDir()) / L"CrashDumps";
        std::error_code ec; fs::create_directories(dir, ec);
        return dir.wstring();
    }

    static std::string Narrow(const std::wstring& w) {
        if (w.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s; s.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
        return s;
    }

    static void WriteTextUtf8(const std::wstring& path, const std::string& data) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
        CloseHandle(h);
    }

    static void AppendOsInfo(std::string& out) {
        using RtlGetVersion_t = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto rtl = reinterpret_cast<RtlGetVersion_t>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtl) {
                RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
                if (rtl(&vi) == 0) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "OS: Windows %u.%u (build %u)\r\n",
                                  (unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion,
                                  (unsigned)vi.dwBuildNumber);
                    out += buf; return;
                }
            }
        }
        out += "OS: Windows (unknown)\r\n";
    }

    static void AppendCpuInfo(std::string& out) {
    #if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        int regs[4] = {0};
        __cpuid(regs, 0);
        char vendor[13] = {};
        *reinterpret_cast<int*>(&vendor[0])  = regs[1];
        *reinterpret_cast<int*>(&vendor[4])  = regs[3];
        *reinterpret_cast<int*>(&vendor[8])  = regs[2];
        vendor[12] = '\0';
        out += "CPU: "; out += vendor; out += "\r\n";
    #else
        out += "CPU: (non-x86)\r\n";
    #endif
    }

    static void AppendBuildInfo(std::string& out) {
    #ifdef CG_BUILD_GIT_HASH
        out += "Build Git: "; out += CG_BUILD_GIT_HASH; out += "\r\n";
    #endif
    #ifdef CG_BUILD_TIME
        out += "Build Time: "; out += CG_BUILD_TIME; out += "\r\n";
    #endif
    }

    static std::wstring BaseName() {
        // App_YYYYMMDD_HHMMSS_mmm_PID_TID
        wchar_t suffix[96]; std::swprintf(suffix, 96, L"_%u_%u",
                                          (unsigned)GetCurrentProcessId(),
                                          (unsigned)GetCurrentThreadId());
        return g_appName + L"_" + Timestamp() + suffix;
    }

    // Keep only the newest N dumps
    static void CleanupOldDumps(const std::wstring& dir, int keep = WINBOOT_MAX_DUMPS) {
        if (keep <= 0) return;
        std::error_code ec;
        fs::path p(dir);
        if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) return;

        std::vector<fs::directory_entry> dumps;
        for (auto& e : fs::directory_iterator(p, ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == L".dmp") dumps.push_back(e);
        }
        if ((int)dumps.size() <= keep) return;

        std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b){
            std::error_code e1, e2;
            auto ta = fs::last_write_time(a, e1);
            auto tb = fs::last_write_time(b, e2);
            if (e1 || e2) return a.path().filename().wstring() < b.path().filename().wstring();
            return ta > tb; // newest first
        });

        for (size_t i = keep; i < dumps.size(); ++i) {
            std::error_code del; fs::remove(dumps[i].path(), del);
        }
    }

    // Call MiniDumpWriteDump (prefer dynamically resolved dbghelp to avoid loader issues)
    using MiniDumpWriteDump_t = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                               CONST PMINIDUMP_EXCEPTION_INFORMATION,
                                               CONST PMINIDUMP_USER_STREAM_INFORMATION,
                                               CONST PMINIDUMP_CALLBACK_INFORMATION);

    static BOOL WriteMiniDump(EXCEPTION_POINTERS* ep, const std::wstring& dumpPath, MINIDUMP_TYPE type) {
        // Try dynamic
        HMODULE hDbg = LoadLibraryW(L"dbghelp.dll");
        MiniDumpWriteDump_t mini = nullptr;
        if (hDbg) mini = reinterpret_cast<MiniDumpWriteDump_t>(GetProcAddress(hDbg, "MiniDumpWriteDump"));

        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (hDbg) FreeLibrary(hDbg);
            return FALSE;
        }

        MINIDUMP_EXCEPTION_INFORMATION mex{};
        mex.ThreadId = GetCurrentThreadId();
        mex.ExceptionPointers = ep;
        mex.ClientPointers = FALSE;

        BOOL ok = FALSE;
        if (mini) {
            ok = mini(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mex, nullptr, nullptr);
        } else {
            // Fallback to link-time function
            ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mex, nullptr, nullptr);
        }
        CloseHandle(hFile);
        if (hDbg) FreeLibrary(hDbg);
        return ok;
    }

    // Top-level handler
    LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) noexcept {
        if (g_inHandler.exchange(true)) {
            // Re-entrant crash; avoid recursion
            return EXCEPTION_EXECUTE_HANDLER;
        }

        __try {
            if (g_dumpDir.empty()) g_dumpDir = ComputeDumpDir();

            fs::path base = fs::path(g_dumpDir) / BaseName();
            fs::path dumpPath = base; dumpPath += L".dmp";
            fs::path txtPath  = base; txtPath  += L".txt";

            MINIDUMP_TYPE mdt =
#if WINBOOT_FULL_DUMP
                (MINIDUMP_TYPE)(MiniDumpWithFullMemory |
                                MiniDumpWithHandleData |
                                MiniDumpWithThreadInfo |
                                MiniDumpWithUnloadedModules |
                                MiniDumpWithFullMemoryInfo |
                                MiniDumpWithIndirectlyReferencedMemory |
                                MiniDumpScanMemory |
                                MiniDumpWithDataSegs);
#else
                (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
                                MiniDumpWithThreadInfo |
                                MiniDumpWithUnloadedModules |
                                MiniDumpScanMemory |
                                MiniDumpWithDataSegs);
#endif

            WriteMiniDump(ep, dumpPath.wstring(), mdt);

            // Human-friendly report
            std::string report;
            report.reserve(1024);
            report += "Application: "; report += Narrow(g_appName); report += "\r\n";
            AppendOsInfo(report);
            AppendCpuInfo(report);
            AppendBuildInfo(report);

            char exc[192];
            std::snprintf(exc, sizeof(exc),
                          "Exception: 0x%08lX at 0x%p\r\nThread: %u  Process: %u\r\n",
                          (unsigned long)ep->ExceptionRecord->ExceptionCode,
                          ep->ExceptionRecord->ExceptionAddress,
                          (unsigned)GetCurrentThreadId(),
                          (unsigned)GetCurrentProcessId());
            report += exc;

            // Append user note if present
            AcquireSRWLockShared(&g_noteLock);
            if (!g_extraNote.empty()) {
                report += "Note: "; report += Narrow(g_extraNote); report += "\r\n";
            }
            ReleaseSRWLockShared(&g_noteLock);

            report += "Dump: "; report += Narrow(dumpPath.wstring()); report += "\r\n";
            WriteTextUtf8(txtPath.wstring(), report);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            // Swallow crashes inside crash handler
        }

        // Chain to previous filter if any
        if (g_prevFilter) return g_prevFilter(ep);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Optional CRT handlers to convert certain failures into SEH so we capture a dump.
    void __stdcall InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
        RaiseException(EXCEPTION_INVALID_PARAMETER, 0, 0, nullptr);
    }
    void __cdecl PureCallHandler() {
        RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0, nullptr);
    }

    // Apply process error-mode according to env/compile-time settings
    void ConfigureErrorModeFromEnv() {
        DWORD mode = GetErrorMode();
        auto env = GetEnvironmentVariableW(L"COLONY_SUPPRESS_WER", nullptr, 0);
        bool suppress = WINBOOT_SUPPRESS_WER || (env > 0); // if env var exists, enable
        if (suppress) {
            mode |= (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
            SetErrorMode(mode);
        }
    }

} // anonymous namespace

namespace winboot {

// Keep original API
void InstallCrashHandler(const std::wstring& appName) {
    if (g_installed.exchange(true)) return;

    g_appName = appName.empty() ? L"ColonyGame" : appName;
    g_dumpDir = ComputeDumpDir();
    CleanupOldDumps(g_dumpDir);

    // Install our SEH filter
    g_prevFilter = ::SetUnhandledExceptionFilter(TopLevelFilter);

    // Convert selected CRT errors to SEH
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _set_purecall_handler(PureCallHandler);

    ConfigureErrorModeFromEnv();
}

// Optional extras — add to CrashHandler.h if you want to use them elsewhere.
void UninstallCrashHandler() {
    if (!g_installed.exchange(false)) return;
    ::SetUnhandledExceptionFilter(g_prevFilter);
    g_prevFilter = nullptr;
}

void SetCrashExtraNote(const std::wstring& note) {
    AcquireSRWLockExclusive(&g_noteLock);
    g_extraNote = note;
    ReleaseSRWLockExclusive(&g_noteLock);
}

const wchar_t* GetCrashDumpDirectory() {
    if (g_dumpDir.empty()) g_dumpDir = ComputeDumpDir();
    return g_dumpDir.c_str();
}

} // namespace winboot
#endif
