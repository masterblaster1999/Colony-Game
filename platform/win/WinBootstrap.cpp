#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "WinBootstrap.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

// Do NOT redefine DPI_AWARENESS_CONTEXT; it's a typedef provided by the Windows SDK.
// Only provide the PER_MONITOR_AWARE_V2 constant if the SDK is missing it.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace {
    // --- State ---
    HANDLE g_mutex = nullptr;
    std::ofstream g_log;
    std::mutex g_logMu;
    std::filesystem::path g_root;
    std::filesystem::path g_dumpDir;

    // --- Small utilities ---
    std::wstring exe_path_w()
    {
        std::wstring buf(260, L'\0');
        for (;;) {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) return L"";
            if (n < buf.size() - 1) { buf.resize(n); return buf; }
            buf.resize(buf.size() * 2);
        }
    }

    std::filesystem::path exe_dir() {
        auto p = std::filesystem::path(exe_path_w());
        return p.empty() ? std::filesystem::current_path() : p.parent_path();
    }

    bool dir_has_assets(const std::filesystem::path& d, const std::wstring& assetDir) {
        std::error_code ec;
        return std::filesystem::exists(d / assetDir, ec);
    }

    std::filesystem::path resolve_root(const std::wstring& assetDir)
    {
        const auto ed = exe_dir();
        const auto parent = ed.parent_path();
        const auto cwd = std::filesystem::current_path();

        for (const auto& d : { ed, parent, cwd }) {
            if (dir_has_assets(d, assetDir)) return d;
        }
        return ed; // fallback: still run, but warn in log
    }

    std::filesystem::path ensure_dir(const std::filesystem::path& p) {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }

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
        if (g_log.is_open()) {
            g_log << "[" << ts_now() << "][" << level << "] " << line << "\n";
            g_log.flush();
        }
    }

    void log_info(const std::string& s)  { log_write("INFO",  s); }
    void log_err (const std::string& s)  { log_write("ERROR", s); }

    void set_dpi_awareness()
    {
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32) {
            using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
            auto pSet = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (pSet) {
                pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                FreeLibrary(user32);
                return;
            }
            FreeLibrary(user32);
        }
        // Fallback available since Vista
        SetProcessDPIAware();
    }

    void maybe_alloc_console(bool enable)
    {
    #if defined(_DEBUG)
        if (!enable) return;
        if (AllocConsole()) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    #else
        (void)enable;
    #endif
    }

    // Crash dumps
    LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t stamp[64];
        swprintf_s(stamp, L"%04u%02u%02u_%02u%02u%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        auto filePath = g_dumpDir / (std::wstring(L"crash_") + stamp + L".dmp");
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers    = FALSE;

        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                    MiniDumpWithFullMemory, &mei, nullptr, nullptr);
        CloseHandle(hFile);

        if (ok) {
            MessageBoxW(nullptr,
                (L"A crash dump was written to:\n" + filePath.wstring()).c_str(),
                L"Colony-Game Crash", MB_OK | MB_ICONERROR);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void install_crash_filter(const std::filesystem::path& dumpDir)
    {
        g_dumpDir = ensure_dir(dumpDir);
        SetUnhandledExceptionFilter(UnhandledFilter);
    }

    // Single-instance (named mutex)
    bool acquire_single_instance(const std::wstring& name)
    {
        const std::wstring full = L"Global\\" + name;
        g_mutex = CreateMutexW(nullptr, TRUE, full.c_str());
        if (!g_mutex) return true; // fail-open: let it run rather than block the user
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (g_mutex) { CloseHandle(g_mutex); g_mutex = nullptr; }
            MessageBoxW(nullptr,
                        L"Colony-Game is already running.",
                        L"Colony-Game", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        return true;
    }
} // anonymous namespace

namespace winboot {

std::filesystem::path GameRoot() { return g_root; }

void Preflight(const Options& opt)
{
    if (opt.makeDpiAware) set_dpi_awareness();

    // Normalize to EXE dir first; then resolve real root that contains assets.
    SetCurrentDirectoryW(exe_dir().c_str());
    g_root = resolve_root(opt.assetDirName);
    std::filesystem::current_path(g_root);

    // Prepare logging + (optional) crash dumps
    const auto logsDir = ensure_dir(g_root / L"logs");
    log_open(logsDir / "launcher.log");
    // Avoid char8_t concatenation: use std::string + path.string()
    log_info(std::string("Bootstrap start. Root: ") + g_root.string());

    if (opt.writeCrashDumps) install_crash_filter(logsDir);

    if (opt.singleInstance && !acquire_single_instance(opt.mutexName)) {
        log_info("Second instance prevented.");
        ExitProcess(0);
    }

    maybe_alloc_console(opt.showConsoleInDebug);

    // Sanity ping about assets
    if (!dir_has_assets(g_root, opt.assetDirName)) {
        log_err("Assets folder '" + std::string(opt.assetDirName.begin(), opt.assetDirName.end()) +
                "' not found; continuing with exe dir.");
    } else {
        log_info(std::string("Assets folder present: ") + (g_root / opt.assetDirName).string());
    }
}

void Shutdown()
{
    log_info("Bootstrap shutdown.");
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
    if (g_log.is_open()) { g_log.flush(); g_log.close(); }
}

} // namespace winboot
