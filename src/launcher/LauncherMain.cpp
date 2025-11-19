// launcher/LauncherMain.cpp
// Windows-only, hardened launcher for Colony-Game.
// Major features:
//  - Single-instance guard + bring existing game window to front.
//  - Correct working directory regardless of how the game is launched.
//  - Robust log capture of the game's stdout/stderr to %LOCALAPPDATA%\ColonyGame\logs (or portable ./logs).
//  - Log retention (by count and total size).
//  - WER LocalDumps auto-enabled for child (mini-dumps by default, full if --fulldump).
//  - Pass-through of command-line args (launcher-only flags are removed).
//  - Optional console mirroring (--console) of child output while still logging.
//  - DLL search path hardening (SetDefaultDllDirectories / SetDllDirectory(L"")).
//  - DPI awareness (Per Monitor v2 when available).
//  - "Kill-on-close" job object so child dies if launcher is terminated.
//  - Elevation fallback if CreateProcess fails with ERROR_ELEVATION_REQUIRED.
//
// Build: C++17+ (C++20 fine). Link: Shell32, Ole32, User32, Advapi32.
// Target type: WIN32 (GUI) by default (see CMake snippet below).

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <shlobj.h>       // SHGetKnownFolderPath
#include <shellapi.h>     // CommandLineToArgvW, ShellExecute
#include <tlhelp32.h>     // CreateToolhelp32Snapshot
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace fs = std::filesystem;

// --------------------------- Configuration ----------------------------------

static constexpr wchar_t kAppName[]           = L"Colony Launcher";
static constexpr wchar_t kCompanyFolder[]     = L"";             // e.g. L"YourStudio" (optional; if set, logs go %LOCALAPPDATA%\YourStudio\ColonyGame\logs)
static constexpr wchar_t kProductFolder[]     = L"ColonyGame";   // used in %LOCALAPPDATA% and messages
static constexpr wchar_t kDefaultGameExe[]    = L"ColonyGame.exe";
static constexpr wchar_t kLauncherIni[]       = L"launcher.ini"; // optional, next to launcher
static constexpr wchar_t kLogPrefix[]         = L"launcher";     // log file prefix

// Single-instance mutex names (Global + Local fallback)
static constexpr wchar_t kMutexNameGlobal[]   = L"Global\\ColonyGame_SingleInstance_Mutex_v2";
static constexpr wchar_t kMutexNameLocal[]    = L"Local\\ColonyGame_SingleInstance_Mutex_v2";

// Log retention defaults
static constexpr size_t  kDefaultKeepLogs     = 20;   // keep newest N logs
static constexpr size_t  kDefaultMaxMB        = 256;  // and/or keep total <= N MB

// --------------------------- Small Utilities --------------------------------

static inline bool IEquals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (towlower(a[i]) != towlower(b[i])) return false;
    return true;
}

static inline std::wstring Hex32(DWORD v) {
    wchar_t b[11]; swprintf(b, 11, L"0x%08X", v); return b;
}

static inline std::wstring GetModuleDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path p = fs::path(buf, buf + n).parent_path();
    return p.wstring();
}

static inline void SetWorkingDirectoryToExeDir() {
    auto d = GetModuleDir();
    SetCurrentDirectoryW(d.c_str());
}

static inline void EnableDpiAwareness() {
    // Try Per-Monitor V2 (Win10+), fall back to system DPI aware.
    typedef BOOL (WINAPI *SetDpiCtx)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto p = reinterpret_cast<SetDpiCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (p) {
            p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    // fallback
    SetProcessDPIAware();
}

static inline void HardenDllSearchPath() {
    // Harden DLL loading to safe defaults.
    typedef BOOL (WINAPI *SetDefaultDllDirectories_t)(DWORD);
    auto p = reinterpret_cast<SetDefaultDllDirectories_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories"));
    if (p) p(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    SetDllDirectoryW(L""); // remove current dir from the search path
}

static inline std::wstring KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR w = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &w))) {
        out = w;
        CoTaskMemFree(w);
    }
    return out;
}

static inline std::wstring TimeStamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u_%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needQuotes = arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needQuotes) return arg;
    std::wstring out; out.reserve(arg.size() + 2);
    out += L'"';
    int bs = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++bs;
        } else if (c == L'"') {
            out.append(bs * 2 + 1, L'\\'); // escape quotes
            out += L'"';
            bs = 0;
        } else {
            if (bs) { out.append(bs, L'\\'); bs = 0; }
            out += c;
        }
    }
    if (bs) out.append(bs * 2, L'\\');
    out += L'"';
    return out;
}

static std::wstring JoinQuoted(const std::vector<std::wstring>& args) {
    std::wstring s;
    bool first = true;
    for (const auto& a : args) {
        if (!first) s += L' ';
        s += QuoteArg(a);
        first = false;
    }
    return s;
}

// --------------------------- Logger -----------------------------------------

struct Logger {
    HANDLE h = INVALID_HANDLE_VALUE;
    std::mutex m;
    bool mirrorToConsole = false;

    explicit Logger(const std::wstring& path, bool mirror = false) : mirrorToConsole(mirror) {
        h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            // UTF-8 BOM optional; skip for brevity
        }
    }
    ~Logger() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    void WriteRaw(const char* data, DWORD n) {
        std::lock_guard<std::mutex> lock(m);
        if (h != INVALID_HANDLE_VALUE && n) {
            DWORD w = 0; WriteFile(h, data, n, &w, nullptr);
        }
        if (mirrorToConsole && n) {
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out && out != INVALID_HANDLE_VALUE) {
                DWORD w = 0; WriteConsoleA(out, data, n, &w, nullptr);
            }
        }
    }

    void Line(const char* fmt, ...) {
        char buf[4096];
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
        va_end(ap);
        if (n < 0) n = 0;
        buf[n++] = '\r'; buf[n++] = '\n';
        WriteRaw(buf, (DWORD)n);
        OutputDebugStringA(buf);
    }

    void LineW(const std::wstring& ws) {
        int need = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8; utf8.resize((size_t)need);
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), utf8.data(), need, nullptr, nullptr);
        Line("%.*s", (int)utf8.size(), utf8.data());
    }
};

static void ApplyLogRetention(const fs::path& dir, size_t keepNewest, size_t maxTotalMB) {
    if (!fs::exists(dir)) return;
    struct Item { fs::path p; uintmax_t size; fs::file_time_type time; };
    std::vector<Item> items;

    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        Item it{ e.path(), 0, {} };
        std::error_code ec;
        it.size = e.file_size(ec);
        it.time = e.last_write_time(ec);
        items.push_back(std::move(it));
    }
    if (items.empty()) return;

    // newest first
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){ return a.time > b.time; });

    uint64_t maxBytes = (uint64_t)maxTotalMB * 1024ull * 1024ull;
    uint64_t acc = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        acc += items[i].size;
        bool overCount = (i + 1) > keepNewest;
        bool overSize  = acc > maxBytes;
        if (overCount || overSize) {
            std::error_code ec;
            fs::remove(items[i].p, ec);
        }
    }
}

// --------------------------- Settings / INI ---------------------------------

struct Settings {
    std::wstring gameExe = kDefaultGameExe;
    bool         singleInstance = true;
    bool         portableLogs   = false; // use ./logs instead of %LOCALAPPDATA%
    int          dumpType       = 1;     // 1 mini, 2 full
    size_t       keepLogs       = kDefaultKeepLogs;
    size_t       maxLogsMB      = kDefaultMaxMB;
    bool         console        = false; // mirror child output to console
};

static void LoadSettingsFromIni(Settings& s, const fs::path& iniPath) {
    wchar_t buf[1024];

    auto G = [&](LPCWSTR key, LPCWSTR deflt)->std::wstring {
        DWORD n = GetPrivateProfileStringW(L"Launcher", key, deflt, buf, (DWORD)std::size(buf), iniPath.c_str());
        return std::wstring(buf, buf + n);
    };
    auto Gi = [&](LPCWSTR key, int deflt)->int {
        DWORD n = GetPrivateProfileStringW(L"Launcher", key, L"", buf, (DWORD)std::size(buf), iniPath.c_str());
        if (n == 0) return deflt;
        return _wtoi(buf);
    };

    std::wstring gx = G(L"GameExe", s.gameExe.c_str());
    if (!gx.empty()) s.gameExe = gx;
    s.singleInstance = Gi(L"SingleInstance", s.singleInstance ? 1 : 0) != 0;
    s.portableLogs   = Gi(L"PortableLogs", s.portableLogs ? 1 : 0) != 0;
    s.dumpType       = Gi(L"DumpType", s.dumpType);
    s.keepLogs       = (size_t)Gi(L"KeepLogs", (int)s.keepLogs);
    s.maxLogsMB      = (size_t)Gi(L"MaxLogsMB", (int)s.maxLogsMB);
    s.console        = Gi(L"Console", s.console ? 1 : 0) != 0;
}

static Settings ParseCommandLineRemoveLauncherFlags(std::wstring& outChildArgs) {
    Settings s;
    // portable mode also enabled if a file "portable.txt" exists next to exe
    if (fs::exists(fs::path(GetModuleDir()) / L"portable.txt")) s.portableLogs = true;

    // INI (optional)
    fs::path ini = fs::path(GetModuleDir()) / kLauncherIni;
    if (fs::exists(ini)) LoadSettingsFromIni(s, ini);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> child;

    auto starts = [&](const std::wstring& a, const wchar_t* pfx) {
        return a.rfind(pfx, 0) == 0;
    };

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (IEquals(a, L"--no-single-instance")) s.singleInstance = false;
        else if (IEquals(a, L"--portable")) s.portableLogs = true;
        else if (IEquals(a, L"--fulldump")) s.dumpType = 2;
        else if (IEquals(a, L"--console")) s.console = true;
        else if (starts(a, L"--game=")) s.gameExe = a.substr(7);
        else child.push_back(std::move(a)); // pass-through to game
    }
    if (argv) LocalFree(argv);

    outChildArgs = JoinQuoted(child);
    return s;
}

// --------------------------- Single Instance --------------------------------

static HANDLE CreateInstanceMutex(const wchar_t* name) {
    HANDLE m = CreateMutexW(nullptr, TRUE, name);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
        return m; // caller inspects GetLastError
    }
    return m;
}

static DWORD GetWindowProcessId(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); return pid;
}

static bool WindowBelongsToProcess(HWND h, DWORD pid) {
    return GetWindowProcessId(h) == pid && IsWindowVisible(h);
}

static BOOL CALLBACK EnumWindowsFindByPid(HWND h, LPARAM lp) {
    DWORD targetPid = (DWORD)lp;
    if (WindowBelongsToProcess(h, targetPid)) {
        // store in GWLP_USERDATA temporarily? Easier: set as foreground now.
        ShowWindowAsync(h, SW_RESTORE);
        AllowSetForegroundWindow(targetPid);
        SetForegroundWindow(h);
        return FALSE; // stop enumeration
    }
    return TRUE;
}

static bool TryBringExistingGameToFront(const std::wstring& gameExeName) {
    // Find running process by exe name then focus its top-level window.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{ sizeof(pe) };
    bool focused = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (IEquals(pe.szExeFile, fs::path(gameExeName).filename().wstring())) {
                EnumWindows(EnumWindowsFindByPid, (LPARAM)pe.th32ProcessID);
                focused = true; // attempt made even if SetForegroundWindow fails due to z-order rules
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return focused;
}

// --------------------------- WER LocalDumps ---------------------------------

static void EnableWerLocalDumpsFor(const std::wstring& exeFilename, const std::wstring& dumpDir, int dumpType /*1=mini,2=full*/) {
    HKEY key;
    std::wstring subkey = L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\" + exeFilename;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        DWORD dt = (dumpType == 2) ? 2u : 1u;
        RegSetValueExW(key, L"DumpType", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dt), sizeof(dt));
        // DumpFolder: REG_EXPAND_SZ, value must include null terminator length
        std::wstring folder = dumpDir;
        DWORD bytes = (DWORD)((folder.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, L"DumpFolder", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(folder.c_str()), bytes);
        RegCloseKey(key);
    }
}

static std::wstring DecodeNtStatus(DWORD code) {
    wchar_t* buf = nullptr;
    std::wstring out;
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (hNt) flags |= FORMAT_MESSAGE_FROM_HMODULE;
    if (FormatMessageW(flags, hNt, code, 0, (LPWSTR)&buf, 0, nullptr) && buf) {
        out = buf; LocalFree(buf);
    }
    return out;
}

// --------------------------- Log Directory ----------------------------------

static fs::path ComputeLogDir(const Settings& s) {
    if (s.portableLogs) {
        fs::path d = fs::path(GetModuleDir()) / L"logs";
        fs::create_directories(d);
        return d;
    }
    std::wstring base = KnownFolderPath(FOLDERID_LocalAppData);
    fs::path d = fs::path(base);
    if (wcslen(kCompanyFolder) > 0) d /= kCompanyFolder;
    d /= kProductFolder; d /= L"logs";
    fs::create_directories(d);
    return d;
}

// --------------------------- Child Process ----------------------------------

struct Handle {
    HANDLE h{ nullptr };
    Handle() = default;
    explicit Handle(HANDLE x) : h(x) {}
    ~Handle(){ if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    operator HANDLE() const { return h; }
    HANDLE* ptr() { return &h; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept { h = o.h; o.h = nullptr; }
    Handle& operator=(Handle&& o) noexcept { if (this!=&o){ if(h&&h!=INVALID_HANDLE_VALUE) CloseHandle(h); h=o.h; o.h=nullptr;} return *this; }
};

static void ReadPipeToLogger(HANDLE readEnd, const char* tag, Logger& log, std::atomic<bool>& stopFlag) {
    char buf[16 * 1024];
    DWORD n = 0;
    std::string prefix = "[" + std::string(tag) + "] ";
    while (!stopFlag) {
        if (!ReadFile(readEnd, buf, sizeof(buf), &n, nullptr)) break;
        if (n == 0) break;
        // Prepend tag prefix to the chunk (may split lines; okay for streaming)
        log.WriteRaw(prefix.data(), (DWORD)prefix.size());
        log.WriteRaw(buf, n);
        // Ensure line break if block didn't end with one (for readability)
        if (buf[n - 1] != '\n') log.WriteRaw("\r\n", 2);
    }
}

static fs::path FindGameExePath(const std::wstring& exeNameOrRelPath) {
    fs::path base = GetModuleDir();
    fs::path p = fs::path(exeNameOrRelPath);
    if (p.is_absolute()) return p;
    p = base / p;
    if (fs::exists(p)) return p;

    // Common fallbacks (bin/, ../)
    fs::path p1 = base / L"bin" / fs::path(exeNameOrRelPath).filename();
    if (fs::exists(p1)) return p1;
    fs::path p2 = base.parent_path() / fs::path(exeNameOrRelPath).filename();
    if (fs::exists(p2)) return p2;

    return p; // probably missing; caller checks
}

// --------------------------- Main -------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnableDpiAwareness();
    HardenDllSearchPath();
    SetWorkingDirectoryToExeDir();

    std::wstring childArgs;
    Settings s = ParseCommandLineRemoveLauncherFlags(childArgs);

    // SINGLE INSTANCE
    Handle m = Handle(CreateInstanceMutex(kMutexNameGlobal));
    if (!m.h) m = Handle(CreateInstanceMutex(kMutexNameLocal)); // fallback
    bool already = (m.h && GetLastError() == ERROR_ALREADY_EXISTS);
    if (s.singleInstance && already) {
        TryBringExistingGameToFront(s.gameExe); // best-effort
        MessageBoxW(nullptr, L"Colony-Game is already running.", kAppName, MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
    }

    // LOGGING
    fs::path logDir = ComputeLogDir(s);
    ApplyLogRetention(logDir, s.keepLogs, s.maxLogsMB);
    fs::path logPath = logDir / (std::wstring(kLogPrefix) + L"_" + TimeStamp() + L".txt");

    // Optional console mirroring (no console window exists in GUI target; create one on demand)
    if (s.console) {
        AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    Logger log(logPath.wstring(), s.console);
    log.Line("Launcher started. Log: %ls", logPath.c_str());
    log.Line("Exe dir: %ls", GetModuleDir().c_str());
    log.Line("Settings: gameExe=\"%ls\" singleInstance=%d portableLogs=%d dumpType=%d keepLogs=%zu maxLogsMB=%zu console=%d",
             s.gameExe.c_str(), (int)s.singleInstance, (int)s.portableLogs, s.dumpType, s.keepLogs, s.maxLogsMB, (int)s.console);

    // GAME PATH
    fs::path gamePath = FindGameExePath(s.gameExe);
    if (!fs::exists(gamePath)) {
        std::wstring msg = L"Could not find the game executable:\n" + gamePath.wstring() +
                           L"\n\nSet GameExe in launcher.ini or use --game=YourGame.exe.";
        log.LineW(L"ERROR: " + msg);
        MessageBoxW(nullptr, msg.c_str(), kAppName, MB_ICONERROR);
        CoUninitialize();
        return 2;
    }
    log.Line("Game path: %ls", gamePath.c_str());

    // WER DUMPS
    EnableWerLocalDumpsFor(gamePath.filename().wstring(), logDir.wstring(), s.dumpType);

    // Create pipes for stdout/stderr capture
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    Handle outR, outW, errR, errW;
    if (!CreatePipe(outR.ptr(), outW.ptr(), &sa, 0) || !CreatePipe(errR.ptr(), errW.ptr(), &sa, 0)) {
        log.Line("WARN: CreatePipe failed; output capture disabled.");
        outR.h = outW.h = errR.h = errW.h = nullptr;
    } else {
        SetHandleInformation(outR.h, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errR.h, HANDLE_FLAG_INHERIT, 0);
    }

    // Build command line for child: "game.exe" + passthrough
    std::wstring cmd = L"\"" + gamePath.wstring() + L"\"";
    if (!childArgs.empty()) { cmd += L" "; cmd += childArgs; }
    log.Line("Command: %ls", cmd.c_str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outW.h ? outW.h : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = errW.h ? errW.h : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    DWORD createFlags = 0; // could use CREATE_NEW_PROCESS_GROUP if desired

    // Job object so child dies if launcher is killed
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (job.h) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    BOOL ok = CreateProcessW(
        nullptr,            // app name
        cmd.data(),         // mutable command line
        nullptr, nullptr,   // security
        TRUE,               // inherit handles (for pipes)
        createFlags,
        nullptr,            // env
        GetModuleDir().c_str(), // working dir
        &si, &pi);

    if (!ok) {
        DWORD e = GetLastError();
        log.Line("CreateProcess failed: %ls (%lu)", Hex32(e).c_str(), e);

        if (e == ERROR_ELEVATION_REQUIRED) {
            // Attempt to relaunch elevated (we cannot capture output in this mode).
            log.Line("Retrying with ShellExecute 'runas' (no log capture).");
            HINSTANCE hinst = ShellExecuteW(nullptr, L"runas", gamePath.c_str(), childArgs.empty() ? nullptr : childArgs.c_str(), GetModuleDir().c_str(), SW_SHOWNORMAL);
            auto code = (INT_PTR)hinst;
            if (code <= 32) {
                std::wstring msg = L"Failed to start the game (elevation).\nError: " + Hex32((DWORD)code) +
                                   L"\n\nTry running as administrator or check your antivirus.";
                log.LineW(L"ERROR: " + msg);
                MessageBoxW(nullptr, msg.c_str(), kAppName, MB_ICONERROR);
                CoUninitialize();
                return (int)code;
            }
            // Success: elevated process created; we can't wait/capture.
            MessageBoxW(nullptr, L"The game was started elevated. Logging is not captured in this mode.", kAppName, MB_ICONINFORMATION);
            CoUninitialize();
            return 0;
        }

        std::wstring emsg = L"Could not start the game.\nWin32 error " + std::to_wstring(e) + L" " + DecodeNtStatus(e);
        MessageBoxW(nullptr, emsg.c_str(), kAppName, MB_ICONERROR);
        CoUninitialize();
        return (int)e;
    }

    // Close our write-ends in the parent so reads complete properly
    if (outW.h) { CloseHandle(outW.h); outW.h = nullptr; }
    if (errW.h) { CloseHandle(errW.h); errW.h = nullptr; }

    // Assign child to job (if available)
    if (job.h) AssignProcessToJobObject(job.h, pi.hProcess);

    std::atomic<bool> stop{ false };
    std::thread tOut, tErr;
    if (outR.h) tOut = std::thread(ReadPipeToLogger, outR.h, "OUT", std::ref(log), std::ref(stop));
    if (errR.h) tErr = std::thread(ReadPipeToLogger, errR.h, "ERR", std::ref(log), std::ref(stop));

    // Wait for child exit
    WaitForSingleObject(pi.hProcess, INFINITE);
    stop = true;

    if (tOut.joinable()) tOut.join();
    if (tErr.joinable()) tErr.join();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // User-friendly crash summary if NTSTATUS-like failure
    if (exitCode >= 0xC0000000) {
        std::wstring detail = DecodeNtStatus(exitCode);
        wchar_t codebuf[32]; swprintf(codebuf, 32, L"0x%08X", exitCode);
        std::wstring msg = L"The game terminated with status " + std::wstring(codebuf) +
                           L".\n" + detail +
                           L"\n\nLogs and (if a crash occurred) a dump file are in:\n" + logDir.wstring();
        log.LineW(L"Child exited with failure status " + std::wstring(codebuf) + L" " + detail);
        MessageBoxW(nullptr, msg.c_str(), kAppName, MB_ICONERROR);
    } else {
        log.Line("Child exited with code %lu", exitCode);
    }

    log.Line("Launcher exiting.");
    if (s.console) {
        log.Line("Press any key to close console...");
        (void)getchar();
        FreeConsole();
    }
    CoUninitialize();
    return (int)exitCode;
}
