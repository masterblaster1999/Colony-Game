// src/win/WinLauncher.cpp
//
// Colony-Game — Enhanced Windows Launcher
// ---------------------------------------
// Massive upgrade over the simple launcher:
//  - Robust command-line parsing & correct Windows quoting when forwarding args
//  - Single-instance enforcement using a short, collision-safe name (hash of game path)
//  - "Bring to front" behavior if already running (no silent failure)
//  - Portable mode: if "portable.txt" exists next to the launcher, logs/dumps live under .\data\
//  - Clean, timestamped logging with automatic rotation (keep last N logs/dumps)
//  - DPI: Per-Monitor-V2 + SHCore fallback
//  - OS error messages include human-readable text (not just codes)
//  - Proper long-path handling (dynamic GetModuleFileNameW buffer + longPathAware-friendly code)
//  - Optional WER (Windows Error Reporting) LocalDumps for the *child game process* (minidumps on crash)
//  - Safer DLL search path (SetDefaultDllDirectories if available)
//  - Optional sleep prevention while waiting for the game to exit
//  - Win32 crash handlers for the launcher (purecall/invalid-parameter/signals -> minidump)
//
// Notes:
//  - This launcher has NO external dependencies beyond Win32 + C++17.
//  - Build as a WIN32 subsystem app (no console).
//  - Link: Dbghelp, Shell32, Ole32, Shcore, Advapi32, User32
//
// Recognized launcher-only flags (they are filtered and NOT forwarded to the game):
//   --launcher-detach           Start the game and exit the launcher immediately (do not wait).
//   --launcher-no-single-instance   Allow multiple instances (disables mutex check).
//   --launcher-no-dumps         Do not enable WER LocalDumps for the child process.
//   --launcher-prevent-sleep    Keep system awake while the child is running.
//
// Environment overrides:
//   COLONY_GAME_EXE             Absolute or relative path to the game exe (overrides search).
//
// Search order for the game exe (if COLONY_GAME_EXE not set and config not provided):
//   1) .\ColonyGame.exe
//   2) .\bin\ColonyGame.exe
//   3) Paths listed in optional .\launcher_config.txt (line: game_exe=PATH)
//
// Portable mode:
//   If ".\portable.txt" exists, data root is ".\data" (created if missing).
//   Otherwise: "%USERPROFILE%\Saved Games\Colony-Game"
//
// Crash dumps & logs live under: <DataRoot>\logs and <DataRoot>\crashdumps
//
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <shlobj.h>     // SHGetKnownFolderPath
#include <shcore.h>     // SetProcessDpiAwareness
#include <dbghelp.h>    // MiniDumpWriteDump
#include <tlhelp32.h>   // CreateToolhelp32Snapshot (bring-to-front helper)
#include <winreg.h>     // Registry (WER LocalDumps)
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <csignal>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace fs = std::filesystem;

// ------------------------------------------------------------
// Utilities
// ------------------------------------------------------------

static std::wstring FormatLastError(DWORD err) {
    if (err == 0) return L"(no error)";
    LPWSTR buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = n ? std::wstring(buf, n) : L"(unknown error)";
    if (buf) LocalFree(buf);
    // Trim trailing newlines/spaces
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ' || msg.back() == L'\t')) {
        msg.pop_back();
    }
    std::wstringstream ss;
    ss << L"[" << err << L"] " << msg;
    return ss.str();
}

static std::wstring NowStampDateTime(bool forFileName = false) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    wchar_t buf[64];
    if (forFileName) {
        // 2025-09-13_14-22-30.123
        swprintf_s(buf, L"%04d-%02d-%02d_%02d-%02d-%02d.%03lld",
                   1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, (long long)ms.count());
    } else {
        // 2025-09-13 14:22:30.123
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                   1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, (long long)ms.count());
    }
    return buf;
}

static std::wstring GetEnvW(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return L"";
    std::wstring s(needed, L'\0');
    DWORD got = GetEnvironmentVariableW(name, s.data(), needed);
    if (got == 0) return L"";
    s.resize(got);
    return s;
}

static std::wstring ModulePath() {
    // Long-path friendly: dynamically grow the buffer up to ~32K wchar
    std::wstring path;
    DWORD size = 512;
    for (;;) {
        std::wstring buf(size, L'\0');
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (len == 0) return L"";
        if (len < buf.size() - 1) {
            buf.resize(len);
            path = std::move(buf);
            break;
        }
        if (size >= 32768) { // give up, return what we have
            buf.resize(len);
            path = std::move(buf);
            break;
        }
        size *= 2;
    }
    return path;
}

static std::wstring ModuleDir() {
    auto p = fs::path(ModulePath());
    return p.remove_filename().wstring();
}

static bool FileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

static std::wstring SavedGamesDir() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &path))) {
        out = path;
        CoTaskMemFree(path);
    } else {
        wchar_t* user = _wgetenv(L"USERPROFILE");
        out = user ? std::wstring(user) + L"\\Saved Games" : L".";
    }
    return out;
}

static bool IsPortableMode() {
    fs::path probe = fs::path(ModuleDir()) / L"portable.txt";
    return FileExists(probe);
}

static fs::path DataRoot() {
    if (IsPortableMode()) {
        return fs::path(ModuleDir()) / L"data";
    }
    return fs::path(SavedGamesDir()) / L"Colony-Game";
}

static void EnsureDir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
}

static void KeepMostRecentFiles(const fs::path& dir, const std::wstring& startsWith,
                                const std::wstring& extension, size_t keepCount) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    struct Node { fs::path p; std::filesystem::file_time_type t; };
    std::vector<Node> items;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        auto name = e.path().filename().wstring();
        if (!startsWith.empty() && name.rfind(startsWith, 0) != 0) continue;
        if (!extension.empty() && e.path().extension().wstring() != extension) continue;
        std::error_code ec2;
        auto tp = fs::last_write_time(e, ec2);
        if (ec2) continue;
        items.push_back({ e.path(), tp });
    }
    std::sort(items.begin(), items.end(), [](const Node& a, const Node& b) { return a.t > b.t; });
    for (size_t i = keepCount; i < items.size(); ++i) {
        std::error_code ec3;
        fs::remove(items[i].p, ec3);
    }
}

static std::wstring BaseName(const std::wstring& path) {
    return fs::path(path).filename().wstring();
}

// 64-bit FNV-1a hash for short, unique mutex name
static uint64_t Hash64(const std::wstring& s) {
    const uint64_t FNV_OFFSET = 1469598103934665603ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t h = FNV_OFFSET;
    for (wchar_t c : s) {
        h ^= (uint64_t)(unsigned)c;
        h *= FNV_PRIME;
    }
    return h;
}

static std::wstring Hex64(uint64_t v) {
    wchar_t buf[17]{};
    swprintf_s(buf, L"%016llX", (unsigned long long)v);
    return buf;
}

// ------------------------------------------------------------
// Logging
// ------------------------------------------------------------

struct Logger {
    fs::path file;
    std::wofstream out;
    explicit Logger(const fs::path& logDir) {
        EnsureDir(logDir);
        // rotate first (keep last 10 logs)
        KeepMostRecentFiles(logDir, L"launcher_", L".log", 10);

        file = logDir / (L"launcher_" + NowStampDateTime(true) + L".log");
        out.open(file);
        if (out) {
            out << L"=== Colony-Game Launcher start ===\n";
            out << L"Time: " << NowStampDateTime(false) << L"\n";
        }
    }
    void line(const std::wstring& s) {
        if (out) {
            out << L"[" << NowStampDateTime(false) << L"] " << s << L"\n";
            out.flush();
        }
    }
    ~Logger() {
        if (out) {
            out << L"=== Launcher end ===\n";
            out.flush();
        }
    }
};

// ------------------------------------------------------------
// Minidumps for the *launcher* itself (not the child)
// ------------------------------------------------------------

static fs::path g_dumpDir;

static bool WriteMiniDump(EXCEPTION_POINTERS* ex, const fs::path& dumpPath) {
    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ex;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), hFile,
        MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
        &mei, nullptr, nullptr);

    CloseHandle(hFile);
    return ok == TRUE;
}

static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ex) {
    fs::path p = g_dumpDir / (L"launcher_crash_" + NowStampDateTime(true) + L".dmp");
    WriteMiniDump(ex, p);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void InstallLauncherCrashHandlers(const fs::path& dumpDir) {
    g_dumpDir = dumpDir;
    EnsureDir(g_dumpDir);

    // Avoid system fault dialog stealing focus (we want our minidump)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(UnhandledFilter);

    // Route CRT-level faults to exceptions so the unhandled filter catches them
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    auto invalid_param_handler = +[](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        RaiseException(0xE0000001 /* custom */, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    };
    _set_invalid_parameter_handler(invalid_param_handler);

    auto purecall_handler = +[]() {
        RaiseException(0xE0000002 /* custom */, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    };
    _set_purecall_handler(purecall_handler);

    // Map critical signals to exceptions
    std::signal(SIGABRT, +[](int) { RaiseException(0xE0000003, EXCEPTION_NONCONTINUABLE, 0, nullptr); });
    std::signal(SIGSEGV, +[](int) { RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr); });
}

// ------------------------------------------------------------
// DPI Awareness
// ------------------------------------------------------------

static void EnableDPI() {
    // Try PerMonitorV2 (User32), then SHCore Per-Monitor (Win 8.1), then legacy
    using SetDpiCtxFn = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto p = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(hUser, "SetProcessDpiAwarenessContext"));
        if (p && p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    // SHCore fallback
    if (SUCCEEDED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) return;

    // Legacy
    SetProcessDPIAware();
}

// ------------------------------------------------------------
// Single instance
// ------------------------------------------------------------

class SingleInstance {
    HANDLE h_ = nullptr;
public:
    explicit SingleInstance(const std::wstring& name) {
        h_ = CreateMutexW(nullptr, TRUE, name.c_str());
    }
    bool acquired() const {
        return h_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~SingleInstance() {
        if (h_) { ReleaseMutex(h_); CloseHandle(h_); }
    }
};

// Bring existing instance (game window) to foreground
static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD searchPid = (DWORD)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == searchPid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        // Try to restore and bring to front
        ShowWindow(hwnd, SW_RESTORE);
        // Bypass foreground lock timeout
        DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        DWORD winThread = GetWindowThreadProcessId(hwnd, nullptr);
        AttachThreadInput(winThread, fgThread, TRUE);
        SetForegroundWindow(hwnd);
        AttachThreadInput(winThread, fgThread, FALSE);
        return FALSE; // stop enumeration
    }
    return TRUE; // continue
}

static void BringProcessWindowsToFront(DWORD pid) {
    EnumWindows(EnumWindowsProc, (LPARAM)pid);
}

static std::vector<DWORD> FindProcessIdsByExeName(const std::wstring& exeName) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// ------------------------------------------------------------
// Safer DLL search path (mitigate DLL hijacking a bit)
// ------------------------------------------------------------

static void TightenDllSearchPath() {
    using Fn = BOOL (WINAPI*)(DWORD);
    HMODULE h = GetModuleHandleW(L"kernel32.dll");
    if (!h) return;
    auto p = reinterpret_cast<Fn>(GetProcAddress(h, "SetDefaultDllDirectories"));
    if (p) {
        // Application dir + system32 + safe dirs, exclude current working dir
        p(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
}

// ------------------------------------------------------------
// WER LocalDumps for the *child game process*
// ------------------------------------------------------------

struct WerKey {
    HKEY key = nullptr;
    bool created = false; // created by us (vs opened existing)
    std::wstring subkeyPath;
    ~WerKey() {
        if (key) RegCloseKey(key);
    }
};

static bool ConfigureWerLocalDumpsForExe(const std::wstring& exeBaseName,
                                         const fs::path& dumpDir,
                                         DWORD dumpType, DWORD dumpCount,
                                         WerKey& out) {
    // HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\<exe>
    const wchar_t* base = L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\";
    out.subkeyPath = std::wstring(base) + exeBaseName;

    DWORD disp = 0;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, out.subkeyPath.c_str(), 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY,
                             nullptr, &out.key, &disp);
    if (r != ERROR_SUCCESS) return false;
    out.created = (disp == REG_CREATED_NEW_KEY);

    std::wstring folder = dumpDir.wstring();
    // DumpFolder (REG_EXPAND_SZ), DumpType (DWORD), DumpCount (DWORD)
    RegSetValueExW(out.key, L"DumpFolder", 0, REG_EXPAND_SZ,
                   reinterpret_cast<const BYTE*>(folder.c_str()),
                   (DWORD)((folder.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(out.key, L"DumpType", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&dumpType), sizeof(DWORD));
    RegSetValueExW(out.key, L"DumpCount", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&dumpCount), sizeof(DWORD));
    return true;
}

static void MaybeCleanupWerKey(const WerKey& k) {
    if (k.key) {
        RegCloseKey(k.key); // closed by dtor anyway
    }
    if (k.created) {
        // Remove only if we created the subkey; leave user's existing config intact
        RegDeleteTreeW(HKEY_CURRENT_USER, k.subkeyPath.c_str());
    }
}

// ------------------------------------------------------------
// Command line handling
// ------------------------------------------------------------

// Correctly quote a single argument according to Windows C runtime parsing rules.
static std::wstring QuoteArgWindows(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needQuotes = arg.find_first_of(L" \t\"") != std::wstring::npos || arg.back() == L'\\';
    if (!needQuotes) return arg;

    std::wstring out;
    out.push_back(L'"');
    unsigned backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            backslashes++;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\'); // escape slashes + the quote
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
            backslashes = 0;
        }
    }
    // Escape trailing backslashes before closing quote
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

struct LaunchOptions {
    bool detach = false;
    bool noSingleInstance = false;
    bool noDumps = false;
    bool preventSleep = false;
    std::vector<std::wstring> forwardedArgs; // to the child
};

// Parse full command line (including exe) then filter out launcher flags
static LaunchOptions ParseLaunchOptions() {
    LaunchOptions L;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc <= 1) {
        if (argv) LocalFree(argv);
        return L;
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto ieq = [](const std::wstring& s1, const std::wstring& s2) {
            return _wcsicmp(s1.c_str(), s2.c_str()) == 0;
        };
        if (ieq(a, L"--launcher-detach"))            { L.detach = true; continue; }
        if (ieq(a, L"--launcher-no-single-instance")){ L.noSingleInstance = true; continue; }
        if (ieq(a, L"--launcher-no-dumps"))          { L.noDumps = true; continue; }
        if (ieq(a, L"--launcher-prevent-sleep"))     { L.preventSleep = true; continue; }
        // Unknown -> forward to child
        L.forwardedArgs.push_back(std::move(a));
    }
    LocalFree(argv);
    return L;
}

static std::wstring JoinForwardedArgs(const std::vector<std::wstring>& args) {
    std::wstring s;
    bool first = true;
    for (auto& a : args) {
        if (!first) s.push_back(L' ');
        s += QuoteArgWindows(a);
        first = false;
    }
    return s;
}

// ------------------------------------------------------------
// Child process launch
// ------------------------------------------------------------

static bool LaunchChild(const fs::path& childExe,
                        const std::vector<std::wstring>& forwardedArgs,
                        std::wofstream* log,
                        bool detach,
                        DWORD* outExitCode) {
    // Build command line: "<ChildExe>" <args...>
    std::wstring cmd = QuoteArgWindows(childExe.wstring());
    if (!forwardedArgs.empty()) {
        cmd.push_back(L' ');
        cmd += JoinForwardedArgs(forwardedArgs);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    fs::path workdir = childExe.parent_path();
    std::wstring wd = workdir.wstring();
    std::wstring mutableCmd = cmd; // CreateProcess may modify this buffer
    if (log) *log << L"[" << NowStampDateTime(false) << L"] Starting: " << mutableCmd << L"\n";

    BOOL ok = CreateProcessW(
        nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | (detach ? DETACHED_PROCESS : 0),
        nullptr, wd.c_str(), &si, &pi);

    if (!ok) {
        if (log) *log << L"[" << NowStampDateTime(false) << L"] CreateProcess failed: "
                      << FormatLastError(GetLastError()) << L"\n";
        return false;
    }

    CloseHandle(pi.hThread);

    if (!detach) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        if (outExitCode) *outExitCode = code;
    }
    CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------------------------------------
// Game exe discovery
// ------------------------------------------------------------

static std::vector<fs::path> CandidateGamePaths() {
    std::vector<fs::path> c;
    fs::path mdir = ModuleDir();
    c.push_back(mdir / L"ColonyGame.exe");
    c.push_back(mdir / L"bin" / L"ColonyGame.exe");

    // Optional: launcher_config.txt with "game_exe=PATH" (relative to launcher dir allowed)
    fs::path cfg = mdir / L"launcher_config.txt";
    if (FileExists(cfg)) {
        std::wifstream in(cfg);
        std::wstring line;
        while (std::getline(in, line)) {
            // Trim spaces
            auto trim = [](std::wstring& s) {
                while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
                while (!s.empty() && iswspace(s.back())) s.pop_back();
            };
            trim(line);
            if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
            const std::wstring key = L"game_exe=";
            if (line.size() > key.size() && _wcsnicmp(line.c_str(), key.c_str(), key.size()) == 0) {
                std::wstring val = line.substr(key.size());
                trim(val);
                fs::path p = val;
                if (p.is_relative()) p = mdir / p;
                c.push_back(p);
            }
        }
    }
    return c;
}

static fs::path FindGameExe(Logger& log) {
    // Environment override
    std::wstring env = GetEnvW(L"COLONY_GAME_EXE");
    if (!env.empty()) {
        fs::path p = env;
        if (p.is_relative()) p = fs::path(ModuleDir()) / p;
        if (FileExists(p)) {
            log.line(L"Using COLONY_GAME_EXE: " + p.wstring());
            return p;
        } else {
            log.line(L"COLONY_GAME_EXE points to non-existent path: " + p.wstring());
        }
    }

    for (auto& p : CandidateGamePaths()) {
        if (FileExists(p)) {
            log.line(L"Found game exe: " + p.wstring());
            return p;
        }
    }
    return {};
}

// ------------------------------------------------------------
// WinMain
// ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR /*lpCmdLine*/, int) {
    // Make DLL search slightly safer early
    TightenDllSearchPath();

    EnableDPI();

    // Data roots
    fs::path dataRoot = DataRoot();
    fs::path logDir   = dataRoot / L"logs";
    fs::path dumpDir  = dataRoot / L"crashdumps";
    EnsureDir(logDir);
    EnsureDir(dumpDir);
    // Rotate dumps too (keep last 10)
    KeepMostRecentFiles(dumpDir, L"crash_", L".dmp", 10);
    KeepMostRecentFiles(dumpDir, L"launcher_crash_", L".dmp", 10);

    Logger logger(logDir);
    logger.line(L"ModuleDir = " + ModuleDir());
    logger.line(std::wstring(L"Portable mode: ") + (IsPortableMode() ? L"ON" : L"OFF"));

    InstallLauncherCrashHandlers(dumpDir);

    // Parse options & build forwarded args
    LaunchOptions opt = ParseLaunchOptions();
    logger.line(std::wstring(L"Options: detach=") + (opt.detach?L"true":L"false")
                + L", noSingleInstance=" + (opt.noSingleInstance?L"true":L"false")
                + L", noDumps=" + (opt.noDumps?L"true":L"false")
                + L", preventSleep=" + (opt.preventSleep?L"true":L"false"));

    // Locate game exe
    fs::path gameExe = FindGameExe(logger);
    if (gameExe.empty()) {
        MessageBoxW(nullptr,
            L"Could not find the game executable.\n"
            L"Tried:\n  .\\ColonyGame.exe\n  .\\bin\\ColonyGame.exe\n"
            L"Or set COLONY_GAME_EXE, or specify in .\\launcher_config.txt (game_exe=...)",
            L"Colony-Game Launcher", MB_OK | MB_ICONERROR);
        return 2;
    }

    // Change our current directory to the game's folder (some tools inspect parent's CWD)
    SetCurrentDirectoryW(gameExe.parent_path().c_str());

    // Single instance
    std::wstring mutexNameSeed = gameExe.wstring(); // use game path (more stable across copies)
    std::wstring mutexName = L"Global\\ColonyGameLauncher_" + Hex64(Hash64(mutexNameSeed));
    SingleInstance inst(mutexName);
    if (!opt.noSingleInstance && !inst.acquired()) {
        // Try to bring the existing game to front
        for (DWORD pid : FindProcessIdsByExeName(BaseName(gameExe.wstring()))) {
            BringProcessWindowsToFront(pid);
        }
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Optional: keep system awake while the child runs
    EXECUTION_STATE oldES = ES_CONTINUOUS;
    if (opt.preventSleep) {
        oldES = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    }

    // Optionally configure WER LocalDumps for the child process
    WerKey wer{};
    if (!opt.noDumps) {
        if (ConfigureWerLocalDumpsForExe(BaseName(gameExe.wstring()), dumpDir, /*DumpType=*/1, /*DumpCount=*/10, wer)) {
            logger.line(L"WER LocalDumps configured under HKCU for " + BaseName(gameExe.wstring()));
        } else {
            logger.line(L"WARNING: Failed to configure WER LocalDumps (child crash dumps may be unavailable).");
        }
    }

    // Launch child
    DWORD exitCode = 0;
    bool ok = LaunchChild(gameExe, opt.forwardedArgs, &logger.out, opt.detach, &exitCode);
    if (ok && !opt.detach) {
        logger.line(L"Child exit code: " + std::to_wstring(exitCode));
    }

    // Clean up WER key if we created one
    if (!opt.noDumps) {
        MaybeCleanupWerKey(wer);
    }

    // Restore execution state if we changed it
    if (opt.preventSleep) {
        SetThreadExecutionState(oldES);
    }

    if (!ok) {
        MessageBoxW(nullptr,
            (L"Failed to start the game:\n" + FormatLastError(GetLastError())).c_str(),
            L"Colony-Game Launcher", MB_OK | MB_ICONERROR);
        return 3;
    }

    return opt.detach ? 0 : (int)exitCode;
}
