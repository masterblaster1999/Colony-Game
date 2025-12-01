// src/platform/win/WinBootstrap.cpp

#include "WinSDK.h"
#include "WinBootstrap.h"

#include <winerror.h>  // ERROR_ALREADY_EXISTS
#include <DbgHelp.h>
#include <filesystem>
#include <optional>     // C++17: std::optional (if used anywhere in this TU)
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <cstdio>      // freopen_s

#pragma comment(lib, "Dbghelp.lib")

// Some older SDKs may not define this; newer ones do in windef.h.
// We only add a fallback if it's missing. DPI_AWARENESS_CONTEXT itself
// comes from the Windows headers included via WinSDK.h.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace
{
    // ---------------------------------------------------------------------
    // Global state
    // ---------------------------------------------------------------------
    HANDLE                g_mutex   = nullptr;
    std::ofstream         g_log;
    std::mutex            g_logMu;
    std::filesystem::path g_root;
    std::filesystem::path g_dumpDir;

    // ---------------------------------------------------------------------
    // UTF-8 helpers
    // ---------------------------------------------------------------------
    std::string wide_to_utf8(std::wstring_view w)
    {
        if (w.empty()) return {};

        int n = ::WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), static_cast<int>(w.size()),
            nullptr, 0, nullptr, nullptr
        );
        if (n <= 0) return {};

        std::string s(static_cast<std::size_t>(n), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), static_cast<int>(w.size()),
            s.data(), n, nullptr, nullptr
        );
        return s;
    }

    std::string path_u8(const std::filesystem::path& p)
    {
        return wide_to_utf8(p.wstring());
    }

    // ---------------------------------------------------------------------
    // Path helpers
    // ---------------------------------------------------------------------
    std::wstring exe_path_w()
    {
        std::wstring buf(260, L'\0');
        for (;;)
        {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0)
                return L"";

            if (n < buf.size() - 1)
            {
                buf.resize(n);
                return buf;
            }

            buf.resize(buf.size() * 2);
        }
    }

    std::filesystem::path exe_dir()
    {
        std::filesystem::path p(exe_path_w());
        return p.empty() ? std::filesystem::current_path() : p.parent_path();
    }

    // Does this directory appear to contain the requested assetDir?
    // assetDir is typically something like L"assets".
    bool dir_has_assets(const std::filesystem::path& root,
                        const std::wstring&          assetDir)
    {
        if (assetDir.empty())
            return false;

        std::error_code ec;

        const auto assetsPath = root / assetDir;
        if (!std::filesystem::exists(assetsPath, ec) ||
            !std::filesystem::is_directory(assetsPath, ec))
        {
            return false;
        }

        // Extra sanity: require a "config" subdir inside the assetDir.
        const auto configPath = assetsPath / L"config";
        if (!std::filesystem::exists(configPath, ec) ||
            !std::filesystem::is_directory(configPath, ec))
        {
            return false;
        }

        return true;
    }

    // ✅ Backward-compatible wrapper for single-argument call sites
    bool dir_has_assets(const std::filesystem::path& root)
    {
        return dir_has_assets(root, std::wstring(L"assets"));
    }

    // Try exe dir, its parent, and CWD; return first that contains assetDir.
    // If none match, fall back to exe dir.
    std::filesystem::path resolve_root(const std::wstring& assetDir)
    {
        const auto ed     = exe_dir();
        const auto parent = ed.parent_path();
        const auto cwd    = std::filesystem::current_path();

        const std::filesystem::path candidates[] = { ed, parent, cwd };

        for (const auto& d : candidates)
        {
            if (!d.empty() && dir_has_assets(d, assetDir))
                return d;
        }

        // Fallback: exe dir; we'll log a warning in Preflight if assets are missing.
        return ed;
    }

    std::filesystem::path ensure_dir(const std::filesystem::path& p)
    {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }

    // ---------------------------------------------------------------------
    // Logging
    // ---------------------------------------------------------------------
    std::string ts_now()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);

        std::tm tm{};
        localtime_s(&tm, &t);

        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf);
    }

    void log_open(const std::filesystem::path& file)
    {
        g_log.open(file, std::ios::out | std::ios::app);
    }

    void log_write(const char* level, const std::string& line)
    {
        std::scoped_lock lk(g_logMu);
        if (g_log.is_open())
        {
            g_log << "[" << ts_now() << "][" << level << "] " << line << "\n";
            g_log.flush();
        }
    }

    void log_info(const std::string& s) { log_write("INFO",  s); }
    void log_err (const std::string& s) { log_write("ERROR", s); }

    // ---------------------------------------------------------------------
    // DLL search path hardening (security)
    // ---------------------------------------------------------------------
    void harden_dll_search(const std::filesystem::path& /*appDir*/)
    {
        // Prefer SetDefaultDllDirectories if available; fall back to removing CWD from search.
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32)
        {
            using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);
            auto pSetDefaults = reinterpret_cast<SetDefaultDllDirectories_t>(
                GetProcAddress(k32, "SetDefaultDllDirectories"));

            if (pSetDefaults)
            {
                // Limit search to system32 + application dir (and any user-added DIRS if needed).
                pSetDefaults(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
                return;
            }
        }
        // Legacy mitigation: remove current directory from the DLL search path.
        SetDllDirectoryW(L"");
    }

    // ---------------------------------------------------------------------
    // Thread annotation (diagnostics)
    // ---------------------------------------------------------------------
    void annotate_current_thread(LPCWSTR name)
    {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (!k32) return;
        using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);
        auto pSetThreadDescription = reinterpret_cast<SetThreadDescription_t>(
            GetProcAddress(k32, "SetThreadDescription"));
        if (pSetThreadDescription)
        {
            pSetThreadDescription(GetCurrentThread(), name);
        }
    }

    // ---------------------------------------------------------------------
    // DPI awareness
    // ---------------------------------------------------------------------
    void set_dpi_awareness()
    {
        // Prefer SetProcessDpiAwarenessContext (Win10+); fall back to SetProcessDPIAware.
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
        {
            using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
            auto pSet = reinterpret_cast<SetDpiCtxFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext")
            );
            if (pSet)
            {
                pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                return;
            }
        }

        // Fallback available since Vista.
        SetProcessDPIAware();
    }

    // ---------------------------------------------------------------------
    // Optional console in debug builds
    // ---------------------------------------------------------------------
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

    // ---------------------------------------------------------------------
    // Crash dumps (re-entrancy-safe)
    // ---------------------------------------------------------------------
    LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
    {
        static volatile LONG s_inFilter = 0;
        if (InterlockedCompareExchange(&s_inFilter, 1, 0) != 0)
        {
            // Already handling a crash — avoid recursion re-entering the dumper.
            return EXCEPTION_EXECUTE_HANDLER;
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);

        wchar_t stamp[64];
        swprintf_s(
            stamp, L"%04u%02u%02u_%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond
        );

        auto filePath = g_dumpDir / (std::wstring(L"crash_") + stamp + L".dmp");

        HANDLE hFile = CreateFileW(
            filePath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile == INVALID_HANDLE_VALUE)
            return EXCEPTION_CONTINUE_SEARCH;

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers    = FALSE;

        // Richer dump for better postmortem analysis (still a MINIDUMP_TYPE).
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithFullMemory |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory |
            MiniDumpWithThreadInfo |
            MiniDumpWithUnloadedModules |
            MiniDumpWithHandleData
        );

        BOOL ok = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            dumpType,
            &mei,
            nullptr,
            nullptr
        );
        CloseHandle(hFile);

        if (ok)
        {
            std::wstring msg = L"A crash dump was written to:\n";
            msg += filePath.wstring();
            MessageBoxW(
                nullptr,
                msg.c_str(),
                L"Colony-Game Crash",
                MB_OK | MB_ICONERROR
            );
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }

    void install_crash_filter(const std::filesystem::path& dumpDir)
    {
        g_dumpDir = ensure_dir(dumpDir);
        SetUnhandledExceptionFilter(UnhandledFilter);
    }

    // ---------------------------------------------------------------------
    // Single-instance via named mutex
    // ---------------------------------------------------------------------
    bool acquire_single_instance(const std::wstring& name)
    {
        const std::wstring full = L"Global\\" + name;
        g_mutex = CreateMutexW(nullptr, TRUE, full.c_str());
        if (!g_mutex)
            return true; // fail-open

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(g_mutex);
            g_mutex = nullptr;

            MessageBoxW(
                nullptr,
                L"Colony-Game is already running.",
                L"Colony-Game",
                MB_OK | MB_ICONINFORMATION
            );
            return false;
        }

        return true;
    }

} // anonymous namespace

// ==========================================================================
// winboot API
// ==========================================================================
namespace winboot
{

std::filesystem::path GameRoot()
{
    return g_root;
}

void Preflight(const Options& opt)
{
    // Process-wide stability & security first.
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    harden_dll_search(exe_dir());
    annotate_current_thread(L"Bootstrap/Main");

    if (opt.makeDpiAware)
        set_dpi_awareness();

    // Normalize to EXE dir first; then resolve real root that contains assets.
    SetCurrentDirectoryW(exe_dir().c_str());
    g_root = resolve_root(opt.assetDirName);
    std::filesystem::current_path(g_root);

    // Prepare logging + (optional) crash dumps.
    const auto logsDir = ensure_dir(g_root / L"logs");
    log_open(logsDir / "launcher.log");
    log_info(std::string("Bootstrap start. Root: ") + path_u8(g_root));

    if (opt.writeCrashDumps)
        install_crash_filter(logsDir);

    if (opt.singleInstance && !acquire_single_instance(opt.mutexName))
    {
        log_info("Second instance prevented.");
        ExitProcess(0);
    }

    maybe_alloc_console(opt.showConsoleInDebug);

    // Sanity check about assets.
    if (!dir_has_assets(g_root, opt.assetDirName))
    {
        log_err(
            std::string("Assets folder '") +
            wide_to_utf8(opt.assetDirName) +
            "' not found; continuing with exe dir."
        );
    }
    else
    {
        log_info(
            std::string("Assets folder present under root: ") +
            path_u8(g_root / opt.assetDirName)
        );
    }
}

void Shutdown()
{
    log_info("Bootstrap shutdown.");

    if (g_mutex)
    {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }

    if (g_log.is_open())
    {
        g_log.flush();
        g_log.close();
    }
}

} // namespace winboot
