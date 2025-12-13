// src/platform/win/WinBootstrap.cpp

#include "WinSDK.h"
#include "WinBootstrap.h"

#include "platform/win/LauncherLogSingletonWin.h" // <-- PATCH: unified process-wide launcher log + WriteLog()

#include <winerror.h>  // ERROR_ALREADY_EXISTS
#include <DbgHelp.h>
#include <filesystem>
#include <optional>     // C++17: std::optional
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <cstdio>      // freopen_s

#pragma comment(lib, "Dbghelp.lib")

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace
{
    HANDLE                g_mutex   = nullptr;
    std::mutex            g_logMu;   // <-- PATCH: keep mutex to avoid interleaved lines across threads
    std::filesystem::path g_root;
    std::filesystem::path g_dumpDir;

    // ---------------- UTF-8 helpers ----------------
    std::string wide_to_utf8(std::wstring_view w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string s((size_t)n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    // PATCH: companion conversion (UTF-8 -> UTF-16)
    std::wstring utf8_to_wide(std::string_view s)
    {
        if (s.empty()) return {};
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
        std::wstring w((size_t)n, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    std::string path_u8(const std::filesystem::path& p) { return wide_to_utf8(p.wstring()); }

    // ---------------- Paths ----------------
    std::wstring exe_path_w()
    {
        std::wstring buf(260, L'\0');
        for (;;)
        {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
            if (n == 0) return L"";
            if (n < buf.size() - 1) { buf.resize(n); return buf; }
            buf.resize(buf.size() * 2);
        }
    }
    std::filesystem::path exe_dir()
    {
        std::filesystem::path p(exe_path_w());
        return p.empty() ? std::filesystem::current_path() : p.parent_path();
    }

    // Return true iff "root/assetDir/config" exists, directories only.
    bool dir_has_assets(const std::filesystem::path& root, const std::wstring& assetDir)
    {
        if (assetDir.empty()) return false;
        std::error_code ec;

        const auto assetsPath = root / assetDir;
        if (!std::filesystem::exists(assetsPath, ec) || !std::filesystem::is_directory(assetsPath, ec))
            return false;

        const auto configPath = assetsPath / L"config";
        if (!std::filesystem::exists(configPath, ec) || !std::filesystem::is_directory(configPath, ec))
            return false;

        return true;
    }

    // Back‑compat wrapper for legacy one‑argument call sites.
    bool dir_has_assets(const std::filesystem::path& root)
    {
        return dir_has_assets(root, std::wstring(L"assets"));
    }

    // ---- Helper that previously triggered C4430/C2143 when 'optional' was unqualified ----
    static bool pick_content_root(std::optional<std::filesystem::path>& opt,
                                  const std::wstring& assetDir)
    {
        if (opt && dir_has_assets(*opt, assetDir))
            return true;

        const auto ed     = exe_dir();
        const auto parent = ed.parent_path();
        const auto cwd    = std::filesystem::current_path();

        const std::filesystem::path candidates[] = { ed, parent, cwd };
        for (const auto& d : candidates)
        {
            if (!d.empty() && dir_has_assets(d, assetDir))
            {
                opt = d;
                return true;
            }
        }
        opt.reset();
        return false;
    }

    std::filesystem::path ensure_dir(const std::filesystem::path& p)
    {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }

    // ---------------- Logging ----------------
    //
    // PATCH: Replace local std::ofstream-based logging with the unified launcher logging
    // backend (OpenLogFile() via LauncherLog()) to avoid duplicated/competing startup logs
    // across WinLauncher / AppMain / WinBootstrap / WinMain.
    //
    // Keep call sites intact (log_open/log_info/log_err), but ignore per-call file paths.
    //
    void log_open(const std::filesystem::path& /*file*/)
    {
        // Force one-time creation/open of the unified launcher-style log stream.
        // (Safe to call multiple times; singleton opens once per process.)
        (void)LauncherLog();
    }

    void log_write(const char* level, const std::string& line)
    {
        std::scoped_lock lk(g_logMu);

        auto& log = LauncherLog();

        const std::wstring wLevel = utf8_to_wide(level ? std::string_view(level) : std::string_view{});
        const std::wstring wLine  = utf8_to_wide(line);

        WriteLog(log, std::wstring(L"[WinBootstrap][") + wLevel + L"] " + wLine);
    }

    void log_info(const std::string& s) { log_write("INFO",  s); }
    void log_err (const std::string& s) { log_write("ERROR", s); }

    // ---------------- Process hardening (safe, no deps) ----------------
    void harden_dll_search()
    {
        if (auto k32 = GetModuleHandleW(L"kernel32.dll"))
        {
            using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);
            if (auto p = reinterpret_cast<SetDefaultDllDirectories_t>(GetProcAddress(k32, "SetDefaultDllDirectories")))
                p(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); // Preferred on Win8+
            else
                SetDllDirectoryW(L"");               // Legacy fallback: remove CWD from DLL path
        }
    } // SetDefaultDllDirectories.

    void set_thread_name(PCWSTR name)
    {
        if (auto k32 = GetModuleHandleW(L"kernel32.dll"))
        {
            using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);
            if (auto p = reinterpret_cast<SetThreadDescription_t>(GetProcAddress(k32, "SetThreadDescription")))
                p(GetCurrentThread(), name);
        }
    } // SetThreadDescription.

    // ---------------- DPI awareness ----------------
    void set_dpi_awareness()
    {
        if (auto user32 = GetModuleHandleW(L"user32.dll"))
        {
            using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
            if (auto p = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
            { p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); return; }
        }
        SetProcessDPIAware(); // Vista fallback
    } // PMv2 guidance.

    // ---------------- Optional console (debug) ----------------
    void maybe_alloc_console(bool enable)
    {
    #if defined(_DEBUG)
        if (!enable) return;
        if (AllocConsole())
        {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    #else
        (void)enable;
    #endif
    }

    // ---------------- Crash dumps (typed MINIDUMP_TYPE) ----------------
    LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
    {
        static volatile LONG s_inFilter = 0;
        if (InterlockedCompareExchange(&s_inFilter, 1, 0) != 0)
            return EXCEPTION_EXECUTE_HANDLER; // already dumping

        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t stamp[64];
        swprintf_s(stamp, L"%04u%02u%02u_%02u%02u%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        auto filePath = g_dumpDir / (std::wstring(L"crash_") + stamp + L".dmp");
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers    = FALSE;

        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithFullMemory |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory |
            MiniDumpWithThreadInfo |
            MiniDumpWithUnloadedModules |
            MiniDumpWithHandleData
        );

        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType,
                                    &mei, nullptr, nullptr);
        CloseHandle(hFile);

        if (ok)
        {
            std::wstring msg = L"A crash dump was written to:\n";
            msg += filePath.wstring();
            MessageBoxW(nullptr, msg.c_str(), L"Colony-Game Crash", MB_OK | MB_ICONERROR);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    } // MiniDumpWriteDump signature / MINIDUMP_TYPE.

    void install_crash_filter(const std::filesystem::path& dumpDir)
    {
        g_dumpDir = ensure_dir(dumpDir);
        SetUnhandledExceptionFilter(UnhandledFilter);
    }

    // ---------------- Single-instance via named mutex ----------------
    bool acquire_single_instance(const std::wstring& name)
    {
        const std::wstring full = L"Global\\" + name;
        g_mutex = CreateMutexW(nullptr, TRUE, full.c_str());
        if (!g_mutex) return true; // fail-open

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(g_mutex); g_mutex = nullptr;
            MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        return true;
    }

} // anon

// ============================== API ==============================
namespace winboot
{
std::filesystem::path GameRoot() { return g_root; }

void Preflight(const Options& opt)
{
    // Process-wide stability & security first.
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0); // fail-fast on heap corruption.
    harden_dll_search();
    set_thread_name(L"Bootstrap/Main");

    if (opt.makeDpiAware) set_dpi_awareness();

    // Normalize to EXE dir; then choose a root that actually contains assets.
    SetCurrentDirectoryW(exe_dir().c_str());
    g_root = exe_dir(); // default
    {
        std::optional<std::filesystem::path> probed = g_root;
        if (pick_content_root(probed, opt.assetDirName) && probed) g_root = *probed;
    }
    std::filesystem::current_path(g_root);

    const auto logsDir = ensure_dir(g_root / L"logs");
    log_open(logsDir / "launcher.log");
    log_info(std::string("Bootstrap start. Root: ") + path_u8(g_root));

    if (opt.writeCrashDumps) install_crash_filter(logsDir);

    if (opt.singleInstance && !acquire_single_instance(opt.mutexName))
    {
        log_info("Second instance prevented.");
        ExitProcess(0);
    }

    maybe_alloc_console(opt.showConsoleInDebug);

    // Asset sanity check.
    if (!dir_has_assets(g_root, opt.assetDirName))
    {
        log_err(std::string("Assets folder '") + wide_to_utf8(opt.assetDirName) +
                "' not found; continuing with exe dir.");
    }
    else
    {
        log_info(std::string("Assets located under: ") + path_u8(g_root / opt.assetDirName));
    }
}

void Shutdown()
{
    log_info("Bootstrap shutdown.");
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }

    // PATCH: Do not close the unified LauncherLog() stream (process-wide singleton).
    // We can flush it best-effort.
    {
        std::scoped_lock lk(g_logMu);
        auto& log = LauncherLog();
        if (log.good())
            log.flush();
    }
}

} // namespace winboot

