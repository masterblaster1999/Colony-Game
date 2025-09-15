// WinLauncher.cpp - Robust, feature-rich Windows-only launcher for Colony-Game
//
// Build as a GUI subsystem (no console window). Unicode throughout.
// C++17 required (for std::wstring_view).
//
// What's new in this upgraded launcher:
//  • Correct Windows argument quoting (handles quotes + trailing backslashes).
//  • Command line includes the program token first (stable argv[0]).
//  • Rich logging with rotation, optional portable/custom log location.
//  • Flexible launcher.ini: target/cwd/priority/mutex/args/env files, portable mode,
//    capture of child stdout/stderr into the log, bring-to-front behavior, safe/repair args.
//  • Optional .env-style environment injection and launcher.args pre-supplied args.
//  • VC++ 2015–2022 (14.x) redist check with friendly, optional installer handoff.
//  • Single-instance guard tries to bring the existing game window to foreground.
//  • Prevents the system/display from sleeping while the game is running (configurable).
//  • Discrete GPU hints (NVIDIA/AMD), safer DLL search, DPI awareness.
//
// Notes:
//  • This file is Windows-only. No Linux/macOS paths are touched.
//  • Keep it in sync with your existing build system (MSVC / CMake).
//  • All changes are additive / conservative to fit into an existing codebase.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shlwapi.h>      // PathRemoveFileSpecW, PathFileExistsW, PathIsRelativeW
#include <shellapi.h>     // CommandLineToArgvW, ShellExecuteExW
#include <shlobj.h>       // SHCreateDirectoryExW, SHGetKnownFolderPath
#include <KnownFolders.h> // FOLDERID_SavedGames
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cwctype>        // towlower
#include <memory>

// --- Bootstrap headers (from platform/win) -----------------------------------
#include "platform/win/WinBootstrapDpi.h"
#include "platform/win/WinBootstrapPaths.h"
// -----------------------------------------------------------------------------

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib") // for CoTaskMemFree / SHGetKnownFolderPath

// --- Back-compat shims for older SDKs ------------------------------------------------------------
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32   0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#endif

// If your SDK is older and doesn't define PER_MONITOR_AWARE_V2, provide a best-effort value.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif
// ----------------------------------------------------------------------------------------------
// Prefer the discrete GPU on hybrid systems (NVIDIA/AMD).
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}
// ----------------------------------------------------------------------------------------------

namespace {

static constexpr const wchar_t* LAUNCHER_VERSION = L"1.4.0";

HANDLE gMutex = nullptr;
HANDLE gJob   = nullptr;

// When set, logs are written here instead of %LOCALAPPDATA%\ColonyGame\logs
static std::wstring gLogCustomRoot;   // e.g., "<moduleDir>\\logs" for portable mode
static std::wstring gModuleDir;       // filled early for convenience

// -------------------------------------------------------------------------------------------------
// Small helpers
// -------------------------------------------------------------------------------------------------

static inline void ToLowerInPlace(std::wstring& s) {
    for (auto& ch : s) ch = towlower(ch);
}

static inline void TrimWhitespaceInPlace(std::wstring& s) {
    const wchar_t* ws = L" \t\r\n";
    size_t a = s.find_first_not_of(ws);
    size_t b = s.find_last_not_of(ws);
    if (a == std::wstring::npos) { s.clear(); return; }
    s = s.substr(a, b - a + 1);
}

static inline void StripOptionalQuotesInPlace(std::wstring& s) {
    if (s.size() >= 2 && ((s.front() == L'"' && s.back() == L'"') ||
                          (s.front() == L'\'' && s.back() == L'\''))) {
        s = s.substr(1, s.size() - 2);
    }
}

static bool ParseBool(std::wstring v, bool defaultValue) {
    TrimWhitespaceInPlace(v); ToLowerInPlace(v);
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"y" || v == L"on") return true;
    if (v == L"0" || v == L"false" || v == L"no"  || v == L"n" || v == L"off") return false;
    return defaultValue;
}

static std::wstring ExpandEnv(const std::wstring& in) {
    DWORD need = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (!need) return in;
    std::wstring out(need, L'\0');
    DWORD got = ExpandEnvironmentStringsW(in.c_str(), &out[0], need);
    if (!got) return in;
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::wstring GetKnownFolderPathW(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) {
        out.assign(p);
        CoTaskMemFree(p);
    }
    return out;
}

std::wstring GetSavedGamesDir() {
    std::wstring s = GetKnownFolderPathW(FOLDERID_SavedGames);
    if (!s.empty()) return s;
    // Fallback: %USERPROFILE%\Saved Games
    return ExpandEnv(L"%USERPROFILE%\\Saved Games");
}

std::wstring GetLastErrorMessage(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = (len && buf) ? std::wstring(buf, len) : L"(no message)";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) msg.pop_back();
    return msg;
}

[[noreturn]] void FailBox(const wchar_t* title, const std::wstring& detail) {
    MessageBoxW(nullptr, detail.c_str(), title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
    ExitProcess(1);
}

void SecureDllSearchOrder() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    using Fn = BOOL (WINAPI*)(DWORD);
    auto pSetDefaultDllDirectories =
        reinterpret_cast<Fn>(GetProcAddress(k32, "SetDefaultDllDirectories"));
    if (pSetDefaultDllDirectories) {
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    } else {
        // Best-effort fallback: remove current directory from the DLL search path.
        SetDllDirectoryW(L"");
    }
}

void SetDpiAwareness() {
    // Use HANDLE-based prototype to avoid SDK-type dependency on DPI_AWARENESS_CONTEXT.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using Fn = BOOL (WINAPI*)(HANDLE);
        if (auto p = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware(); // fallback on very old systems
}

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error",
                L"GetModuleFileNameW failed. Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }
    PathRemoveFileSpecW(path);
    return path;
}

bool Exists(const std::wstring& p) {
    return PathFileExistsW(p.c_str()) != FALSE;
}

std::wstring DirNameFromPath(const std::wstring& absPath) {
    size_t p = absPath.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    return absPath.substr(0, p);
}

// -------------------------------------------------------------------------------------------------
// Logging (with rotation + optional portable/custom root)
// -------------------------------------------------------------------------------------------------

static constexpr ULONGLONG kLogRotateThresholdBytes = 512ull * 1024ull; // 512 KiB

std::wstring GetLocalAppData() {
    wchar_t buf[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return L"";
    return std::wstring(buf);
}

void EnsureDirRecursive(const std::wstring& dir) {
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr); // ok if exists
}

void SetLogCustomRoot(const std::wstring& root) {
    gLogCustomRoot = root;
    if (!gLogCustomRoot.empty()) EnsureDirRecursive(gLogCustomRoot);
}

std::wstring LogsRoot() {
    if (!gLogCustomRoot.empty()) return gLogCustomRoot;
    std::wstring d = GetLocalAppData();
    if (d.empty()) return L"";
    d += L"\\ColonyGame\\logs";
    EnsureDirRecursive(d);
    return d;
}

bool GetFileSizeBytes(const std::wstring& path, ULONGLONG& out) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    out = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return true;
}

void RotateLogIfNeeded(const std::wstring& path) {
    ULONGLONG sz = 0;
    if (GetFileSizeBytes(path, sz) && sz >= kLogRotateThresholdBytes) {
        std::wstring prev = path;
        size_t dot = prev.find_last_of(L'.');
        if (dot != std::wstring::npos) prev.insert(dot, L".prev");
        else                           prev += L".prev";
        DeleteFileW(prev.c_str());
        MoveFileW(path.c_str(), prev.c_str());
    }
}

void Log(const wchar_t* fmt, ...) {
    std::wstring dir = LogsRoot();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\launcher.log";

    RotateLogIfNeeded(path);

    wchar_t line[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, sizeof(line)/sizeof(line[0]), _TRUNCATE, fmt, ap);
    va_end(ap);

    if (FILE* f = _wfopen(path.c_str(), L"a+, ccs=UTF-8")) {
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(f, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
        fclose(f);
    }
}

// -------------------------------------------------------------------------------------------------
// Config/Target discovery & extras (launcher.ini / .args / .env)
// -------------------------------------------------------------------------------------------------

struct LauncherConfig {
    // Core
    std::wstring target;                    // target exe path (relative or absolute)
    std::wstring cwd;                       // "", "auto", "module_dir", or explicit path
    std::wstring priority;                  // "normal", "abovenormal", "high"
    std::wstring mutexName = L"Global\\ColonyGame_SingleInstance";

    // Files
    std::wstring argsFile = L"launcher.args";  // optional; one arg per line
    std::wstring envFile  = L"launcher.env";   // optional; KEY=VALUE per line
    bool argsFilePrepend  = true;              // file args before user args

    // Logging
    bool portable          = false;            // if true, logs under <moduleDir>\logs
    std::wstring logsDir;                      // optional custom logs root (abs or relative)

    // Output capture
    bool captureOutput     = false;            // capture child's stdout/stderr into logs
    bool captureStderr     = true;             // if capturing, also capture stderr (merged)
    std::wstring captureCodepage = L"UTF-8";   // "UTF-8", "OEM", "ACP", or codepage number (e.g., "65001")

    // QoL
    bool keepDisplayAwake  = true;             // prevent sleep while game runs
    std::wstring safeArgs;                     // appended when SHIFT is held at launcher start
    std::wstring repairArgs;                   // appended when CTRL is held at launcher start
    std::wstring bringTitleHint = L"Colony";   // substring to find when bringing to front
    std::wstring appUserModelID = L"ColonyGame.Launcher"; // taskbar grouping / notifications

    // Prereqs
    bool requireVcRedist   = true;             // check/install VC++ redist (if redistributable present)
    std::wstring redistDir = L"redist";        // where vc_redist.*.exe lives (relative to launcher)

    // Saves directory (optional QoL)
    bool ensureSavesDir    = false;
    std::wstring savesDir  = L"$(SavedGames)\\ColonyGame";
};

bool ReadLauncherConfig(const std::wstring& moduleDir, LauncherConfig& cfg) {
    std::wstring ini = moduleDir + L"\\launcher.ini";
    if (!Exists(ini)) return false;

    std::wifstream f(ini);
    if (!f) return false;

    std::wstring line;
    while (std::getline(f, line)) {
        std::wstring trimmed = line;
        TrimWhitespaceInPlace(trimmed);
        if (trimmed.empty()) continue;
        if (trimmed[0] == L'#' || trimmed[0] == L';') continue;
        if (trimmed.rfind(L"//", 0) == 0) continue;

        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = trimmed.substr(0, eq);
        std::wstring val = trimmed.substr(eq + 1);
        TrimWhitespaceInPlace(key);
        TrimWhitespaceInPlace(val);
        StripOptionalQuotesInPlace(val);

        std::wstring lkey = key; ToLowerInPlace(lkey);

        if (lkey == L"target") {
            cfg.target = ExpandEnv(val);
        } else if (lkey == L"cwd" || lkey == L"workingdir") {
            cfg.cwd = ExpandEnv(val);
        } else if (lkey == L"priority") {
            cfg.priority = val; ToLowerInPlace(cfg.priority);
        } else if (lkey == L"argsfile") {
            cfg.argsFile = val;
        } else if (lkey == L"envfile") {
            cfg.envFile = val;
        } else if (lkey == L"args_order") {
            std::wstring v = val; ToLowerInPlace(v);
            cfg.argsFilePrepend = (v != L"append");
        } else if (lkey == L"mutex" || lkey == L"mutexname") {
            cfg.mutexName = val;
        } else if (lkey == L"require_vc_redist" || lkey == L"requirevcredist") {
            cfg.requireVcRedist = ParseBool(val, cfg.requireVcRedist);
        } else if (lkey == L"redist_dir") {
            cfg.redistDir = val;
        } else if (lkey == L"portable") {
            cfg.portable = ParseBool(val, cfg.portable);
        } else if (lkey == L"logs_dir") {
            cfg.logsDir = val;
        } else if (lkey == L"capture_output") {
            cfg.captureOutput = ParseBool(val, cfg.captureOutput);
        } else if (lkey == L"capture_stderr") {
            cfg.captureStderr = ParseBool(val, cfg.captureStderr);
        } else if (lkey == L"capture_codepage") {
            cfg.captureCodepage = val;
        } else if (lkey == L"keep_display_awake") {
            cfg.keepDisplayAwake = ParseBool(val, cfg.keepDisplayAwake);
        } else if (lkey == L"safe_args") {
            cfg.safeArgs = val;
        } else if (lkey == L"repair_args") {
            cfg.repairArgs = val;
        } else if (lkey == L"bring_title_hint") {
            cfg.bringTitleHint = val;
        } else if (lkey == L"app_user_model_id") {
            cfg.appUserModelID = val;
        } else if (lkey == L"ensure_saves_dir") {
            cfg.ensureSavesDir = ParseBool(val, cfg.ensureSavesDir);
        } else if (lkey == L"saves_dir") {
            cfg.savesDir = val;
        }
    }
    return true;
}

bool ReadLauncherTargetFromIniLegacy(const std::wstring& moduleDir, std::wstring& outExeRel) {
    LauncherConfig tmp{};
    if (!ReadLauncherConfig(moduleDir, tmp)) return false;
    if (tmp.target.empty()) return false;
    outExeRel = tmp.target;
    return true;
}

// Token expansion for config-driven paths
static std::wstring ExpandTokens(std::wstring s, const std::wstring& moduleDir, const std::wstring& exeDir) {
    s = ReplaceAll(s, L"$(ModuleDir)", moduleDir);
    s = ReplaceAll(s, L"$(ExeDir)", exeDir);
    s = ReplaceAll(s, L"$(SavedGames)", GetSavedGamesDir());
    s = ExpandEnv(s);
    return s;
}

std::wstring FindGameExe(const std::wstring& moduleDir, std::wstring configuredTarget = L"") {
    // 1) user-configured target
    if (!configuredTarget.empty()) {
        std::wstring rel = configuredTarget;
        std::wstring abs = rel;
        if (!PathIsRelativeW(rel.c_str())) {
            if (Exists(abs)) return abs;
        } else {
            abs = moduleDir + L"\\" + rel;
            if (Exists(abs)) return abs;
        }
        Log(L"launcher.ini target not found: %s", rel.c_str());
    } else {
        std::wstring rel;
        if (ReadLauncherTargetFromIniLegacy(moduleDir, rel)) {
            std::wstring abs = rel;
            if (!PathIsRelativeW(rel.c_str())) {
                if (Exists(abs)) return abs;
            } else {
                abs = moduleDir + L"\\" + rel;
                if (Exists(abs)) return abs;
            }
            Log(L"launcher.ini legacy target not found: %s", rel.c_str());
        }
    }

    // 2) common defaults (adjust if your actual exe name differs)
    const wchar_t* candidates[] = {
        L"ColonyGame.exe",
        L"Colony-Game.exe",
        L"Colony.exe",
        L"Game.exe",
        L"bin\\ColonyGame.exe",
        L"build\\ColonyGame.exe",
        L"bin\\Release\\ColonyGame.exe",
        L"bin\\Debug\\ColonyGame.exe",
        L"build\\Release\\ColonyGame.exe",
        L"build\\Debug\\ColonyGame.exe"
    };
    for (auto c : candidates) {
        std::wstring abs = moduleDir + L"\\" + c;
        if (Exists(abs)) return abs;
    }

    // 3) last resort: first *.exe next to the launcher that is not the launcher itself
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((moduleDir + L"\\*.exe").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcsstr(fd.cFileName, L"Launcher") == nullptr) {
                std::wstring abs = moduleDir + L"\\" + fd.cFileName;
                if (Exists(abs)) { FindClose(h); return abs; }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return L"";
}

// -------------------------------------------------------------------------------------------------
// Correct Windows argument quoting per CommandLineToArgvW / MSVC rules
// -------------------------------------------------------------------------------------------------

static void AppendQuotedArg(std::wstring& out, std::wstring_view arg) {
    const bool needsQuotes = arg.empty() ||
                             arg.find_first_of(L" \t\"") != std::wstring_view::npos;
    if (!needsQuotes) { out.append(arg); return; }
    out.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            // double the backslashes, then escape the quote
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            if (backslashes) { out.append(backslashes, L'\\'); backslashes = 0; }
            out.push_back(ch);
        }
    }
    if (backslashes) out.append(backslashes * 2, L'\\'); // double trailing backslashes
    out.push_back(L'"');
}

static std::wstring BuildArgsFromVector(const std::vector<std::wstring>& args) {
    std::wstring out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out.push_back(L' ');
        AppendQuotedArg(out, args[i]);
    }
    return out;
}

// -------------------------------------------------------------------------------------------------
// Args / Env file support
// -------------------------------------------------------------------------------------------------

static std::vector<std::wstring> ReadArgsFile(const std::wstring& path) {
    std::vector<std::wstring> out;
    std::wifstream f(path);
    if (!f) return out;

    std::wstring line;
    while (std::getline(f, line)) {
        std::wstring s = line;
        TrimWhitespaceInPlace(s);
        if (s.empty()) continue;
        if (s[0] == L'#' || s[0] == L';') continue;
        if (s.rfind(L"//", 0) == 0) continue;
        out.emplace_back(std::move(s));
    }
    return out;
}

static void LoadEnvFileAndApply(const std::wstring& path,
                                const std::wstring& moduleDir,
                                const std::wstring& exeDir) {
    std::wifstream f(path);
    if (!f) return;

    std::wstring line;
    while (std::getline(f, line)) {
        std::wstring s = line;
        TrimWhitespaceInPlace(s);
        if (s.empty()) continue;
        if (s[0] == L'#' || s[0] == L';') continue;
        if (s.rfind(L"//", 0) == 0) continue;

        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = s.substr(0, eq);
        std::wstring val = s.substr(eq + 1);
        TrimWhitespaceInPlace(key);
        TrimWhitespaceInPlace(val);
        StripOptionalQuotesInPlace(val);

        if (key.empty()) continue;

        std::wstring expanded = ExpandTokens(val, moduleDir, exeDir);
        SetEnvironmentVariableW(key.c_str(), expanded.c_str());
        Log(L"ENV set: %s = %s", key.c_str(), expanded.c_str());
    }
}

// -------------------------------------------------------------------------------------------------
// Job object: kill child if launcher dies
// -------------------------------------------------------------------------------------------------

bool SetupKillOnCloseJob() {
    gJob = CreateJobObjectW(nullptr, nullptr);
    if (!gJob) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return !!SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
}

// -------------------------------------------------------------------------------------------------
// VC++ Redistributable detection & installer handoff (best-effort)
// -------------------------------------------------------------------------------------------------

// Determine if the target exe is 32-bit or 64-bit.
enum class ExeArch { Unknown, X86, X64 };
ExeArch GetExeArch(const std::wstring& exePath) {
    DWORD type = 0;
    if (!GetBinaryTypeW(exePath.c_str(), &type)) return ExeArch::Unknown;
    if (type == SCS_64BIT_BINARY) return ExeArch::X64;
    if (type == SCS_32BIT_BINARY) return ExeArch::X86;
    return ExeArch::Unknown;
}

// Check the registry for VC++ 2015-2022 redist (14.x) presence.
bool IsVcRedistInstalled(bool wantX64) {
    HKEY hKey = nullptr;
    const wchar_t* subkey = wantX64
        ? L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X64"
        : L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X86";

    REGSAM sam = KEY_READ;
#if defined(_WIN64)
    if (!wantX64) sam |= KEY_WOW64_32KEY;
#else
    if (wantX64) sam |= KEY_WOW64_64KEY;
#endif

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, sam, &hKey) != ERROR_SUCCESS) return false;

    DWORD installed = 0, cb = sizeof(installed);
    LONG res = RegQueryValueExW(hKey, L"Installed", nullptr, nullptr, reinterpret_cast<LPBYTE>(&installed), &cb);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS && installed == 1);
}

bool RunVcRedistInstallerAndWait(const std::wstring& installerPath) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = installerPath.c_str();
    sei.lpParameters = L"/quiet /norestart";
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        Log(L"Failed to start VC++ installer: %s (err=%lu)", installerPath.c_str(), GetLastError());
        return false;
    }
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        Log(L"VC++ installer exited with code %lu", (unsigned long)code);
        return true; // treat as best-effort regardless of code
    }
    return false;
}

void MaybeEnsureVcRedist(const LauncherConfig& cfg,
                         const std::wstring& moduleDir,
                         ExeArch arch) {
    if (!cfg.requireVcRedist) return;
    if (arch == ExeArch::Unknown) return;

    const bool wantX64 = (arch == ExeArch::X64);
    if (IsVcRedistInstalled(wantX64)) {
        Log(L"VC++ Redist (14.x) appears installed for %s.", wantX64 ? L"x64" : L"x86");
        return;
    }

    std::wstring installer = moduleDir + L"\\" + cfg.redistDir + L"\\vc_redist." + (wantX64 ? L"x64" : L"x86") + L".exe";
    if (!Exists(installer)) {
        Log(L"VC++ Redist seems missing and no installer found at: %s", installer.c_str());
        return;
    }

    std::wstring text = L"The Microsoft Visual C++ Redistributable (" +
                        std::wstring(wantX64 ? L"x64" : L"x86") +
                        L") may be missing.\n\nInstall it now?\n\nInstaller:\n" + installer;
    int ret = MessageBoxW(nullptr, text.c_str(), L"Colony-Game - Prerequisite",
                          MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND | MB_TASKMODAL);
    if (ret == IDYES) {
        RunVcRedistInstallerAndWait(installer);
    }
}

// -------------------------------------------------------------------------------------------------
// Bring an already-running game window to the foreground (best-effort)
// -------------------------------------------------------------------------------------------------

struct BringToFrontCtx {
    std::wstring titleHintLower;
    std::wstring moduleDirLower;
    HWND found = nullptr;
};

BOOL CALLBACK EnumWindowsProcBringToFront(HWND hwnd, LPARAM lparam) {
    BringToFrontCtx* ctx = reinterpret_cast<BringToFrontCtx*>(lparam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    // Title check
    wchar_t title[512];
    GetWindowTextW(hwnd, title, 512);
    std::wstring t = title;
    std::wstring tl = t; ToLowerInPlace(tl);
    if (!ctx->titleHintLower.empty() && tl.find(ctx->titleHintLower) != std::wstring::npos) {
        ctx->found = hwnd;
        return FALSE; // stop
    }

    // Fallback: process path under moduleDir
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t path[MAX_PATH];
            DWORD sz = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
                std::wstring p = path; std::wstring pl = p; ToLowerInPlace(pl);
                if (!ctx->moduleDirLower.empty() && pl.find(ctx->moduleDirLower) == 0) {
                    ctx->found = hwnd;
                    CloseHandle(h);
                    return FALSE;
                }
            }
            CloseHandle(h);
        }
    }
    return TRUE;
}

void TryBringExistingToFront(const std::wstring& titleHint, const std::wstring& moduleDir) {
    BringToFrontCtx ctx;
    ctx.titleHintLower = titleHint; ToLowerInPlace(ctx.titleHintLower);
    ctx.moduleDirLower = moduleDir; ToLowerInPlace(ctx.moduleDirLower);

    EnumWindows(EnumWindowsProcBringToFront, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found) {
        AllowSetForegroundWindow(ASFW_ANY);
        ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
        BringWindowToTop(ctx.found);
    }
}

// -------------------------------------------------------------------------------------------------
// Child stdout/stderr capture (optional)
// -------------------------------------------------------------------------------------------------

UINT ResolveCodepageFromName(std::wstring name) {
    TrimWhitespaceInPlace(name); ToLowerInPlace(name);
    if (name == L"utf-8" || name == L"utf8") return CP_UTF8;
    if (name == L"oem") return GetOEMCP();
    if (name == L"acp") return GetACP();
    // Try numeric
    wchar_t* end = nullptr;
    unsigned long n = wcstoul(name.c_str(), &end, 10);
    if (end && *end == 0 && n >= 1 && n <= 65535) return static_cast<UINT>(n);
    return CP_UTF8;
}

struct PipeCapture {
    bool enabled = false;
    HANDLE hOutR = nullptr, hOutW = nullptr;
    HANDLE hErrR = nullptr, hErrW = nullptr;
    HANDLE hThreadOut = nullptr, hThreadErr = nullptr;
    UINT codepage = CP_UTF8;
};

DWORD WINAPI PipeReaderThread(LPVOID param) {
    auto* pair = reinterpret_cast<std::pair<HANDLE, UINT>*>(param);
    HANDLE hRead = pair->first;
    UINT cp = pair->second;
    std::unique_ptr<std::pair<HANDLE, UINT>> guard(pair); // auto-delete

    const DWORD kBuf = 4096;
    std::string acc; acc.reserve(kBuf * 2);

    for (;;) {
        char buf[kBuf];
        DWORD got = 0;
        BOOL ok = ReadFile(hRead, buf, kBuf, &got, nullptr);
        if (!ok || got == 0) break;
        acc.append(buf, buf + got);

        // Emit complete lines
        size_t pos = 0;
        for (;;) {
            size_t nl = acc.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = acc.substr(pos, (nl - pos));
            pos = nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Convert to wide and log
            int wlen = MultiByteToWideChar(cp, 0, line.data(), (int)line.size(), nullptr, 0);
            if (wlen <= 0) {
                Log(L"[GAME] %S", line.c_str()); // fallback: raw bytes
            } else {
                std::wstring w(wlen, L'\0');
                MultiByteToWideChar(cp, 0, line.data(), (int)line.size(), &w[0], wlen);
                Log(L"[GAME] %s", w.c_str());
            }
        }
        // keep remainder
        if (pos > 0) acc.erase(0, pos);
    }

    // Flush remainder
    if (!acc.empty()) {
        int wlen = MultiByteToWideChar(cp, 0, acc.data(), (int)acc.size(), nullptr, 0);
        if (wlen <= 0) {
            Log(L"[GAME] %S", acc.c_str());
        } else {
            std::wstring w(wlen, L'\0');
            MultiByteToWideChar(cp, 0, acc.data(), (int)acc.size(), &w[0], wlen);
            Log(L"[GAME] %s", w.c_str());
        }
    }
    return 0;
}

void SetupCaptureIfEnabled(PipeCapture& cap, const LauncherConfig& cfg, STARTUPINFOW& si, DWORD& createFlags, BOOL& inheritHandles) {
    if (!cfg.captureOutput) return;
    cap.enabled = true;
    cap.codepage = ResolveCodepageFromName(cfg.captureCodepage);

    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    if (!CreatePipe(&cap.hOutR, &cap.hOutW, &sa, 0)) {
        Log(L"CreatePipe stdout failed: %lu", GetLastError());
        cap.enabled = false;
        return;
    }
    SetHandleInformation(cap.hOutR, HANDLE_FLAG_INHERIT, 0);

    if (cfg.captureStderr) {
        if (!CreatePipe(&cap.hErrR, &cap.hErrW, &sa, 0)) {
            Log(L"CreatePipe stderr failed: %lu", GetLastError());
            CloseHandle(cap.hOutR); CloseHandle(cap.hOutW);
            cap.enabled = false;
            return;
        }
        SetHandleInformation(cap.hErrR, HANDLE_FLAG_INHERIT, 0);
    }

    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = cap.hOutW;
    si.hStdError  = cfg.captureStderr ? cap.hErrW : cap.hOutW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE); // optional

    inheritHandles = TRUE;
    (void)createFlags; // unused here, reserved for future
}

void StartCaptureThreadsIfNeeded(PipeCapture& cap) {
    if (!cap.enabled) return;
    // Close our write ends to avoid deadlock; child holds them
    if (cap.hOutW) { CloseHandle(cap.hOutW); cap.hOutW = nullptr; }
    if (cap.hErrW) { CloseHandle(cap.hErrW); cap.hErrW = nullptr; }

    if (cap.hOutR) {
        auto* ctx = new std::pair<HANDLE, UINT>(cap.hOutR, cap.codepage);
        cap.hThreadOut = CreateThread(nullptr, 0, PipeReaderThread, ctx, 0, nullptr);
    }
    if (cap.hErrR) {
        auto* ctx = new std::pair<HANDLE, UINT>(cap.hErrR, cap.codepage);
        cap.hThreadErr = CreateThread(nullptr, 0, PipeReaderThread, ctx, 0, nullptr);
    }
}

void StopCaptureThreads(PipeCapture& cap) {
    if (!cap.enabled) return;
    if (cap.hThreadOut) { WaitForSingleObject(cap.hThreadOut, INFINITE); CloseHandle(cap.hThreadOut); cap.hThreadOut = nullptr; }
    if (cap.hThreadErr) { WaitForSingleObject(cap.hThreadErr, INFINITE); CloseHandle(cap.hThreadErr); cap.hThreadErr = nullptr; }
    if (cap.hOutR) { CloseHandle(cap.hOutR); cap.hOutR = nullptr; }
    if (cap.hErrR) { CloseHandle(cap.hErrR); cap.hErrR = nullptr; }
}

// -------------------------------------------------------------------------------------------------
// Elevated fallback using ShellExecuteEx (wait + exit code)
// -------------------------------------------------------------------------------------------------

bool TryElevatedLaunch(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, DWORD& exitCodeOut) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = nullptr;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str(); // args-only for ShellExecuteExW
    sei.lpDirectory = cwd.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD e = GetLastError();
        Log(L"ShellExecuteExW(runas) failed: %lu (%s)", e, GetLastErrorMessage(e).c_str());
        return false;
    }

    if (sei.hProcess) {
        if (gJob) {
            if (!AssignProcessToJobObject(gJob, sei.hProcess)) {
                Log(L"AssignProcessToJobObject (elevated) failed: %lu", GetLastError());
            }
        }
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        exitCodeOut = code;
    } else {
        exitCodeOut = 0;
    }
    return true;
}

// -------------------------------------------------------------------------------------------------
// Core launch routine
// -------------------------------------------------------------------------------------------------

DWORD PriorityClassFromString(const std::wstring& s) {
    std::wstring out = s; ToLowerInPlace(out);
    if (out == L"high")         return HIGH_PRIORITY_CLASS;
    if (out == L"abovenormal")  return ABOVE_NORMAL_PRIORITY_CLASS;
    if (out == L"belownormal")  return BELOW_NORMAL_PRIORITY_CLASS;
    if (out == L"idle")         return IDLE_PRIORITY_CLASS;
    return NORMAL_PRIORITY_CLASS;
}

bool LaunchGame(const std::wstring& exePath,
                const std::wstring& argsTail,
                const std::wstring& workingDir,
                DWORD priorityClass,
                const LauncherConfig& cfg,
                DWORD& childExitCode)
{
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT; // GUI game; no console needed
    BOOL inheritHandles = FALSE;

    // Optional capture: sets STARTF_USESTDHANDLES & inheritHandles=TRUE
    PipeCapture capture{};
    SetupCaptureIfEnabled(capture, cfg, si, flags, inheritHandles);

    // Build full command line: include program token so the child sees a sane argv[0].
    std::wstring cmd;
    AppendQuotedArg(cmd, exePath);
    if (!argsTail.empty()) { cmd.push_back(L' '); cmd.append(argsTail); }

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    if (mutableCmd.size() > 32767) {
        FailBox(L"Launch Failed", L"Command line too long for CreateProcessW.");
    }

    SecureDllSearchOrder();

    BOOL ok = CreateProcessW(
        exePath.c_str(),                  // lpApplicationName (explicit path)
        mutableCmd.data(),                // lpCommandLine (program + args)
        nullptr, nullptr,
        inheritHandles,                   // inherit handles if capturing I/O
        flags,
        nullptr,                          // inherit environment
        workingDir.c_str(),               // working directory
        &si, &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        Log(L"CreateProcessW failed (%lu: %s) for: %s", e, GetLastErrorMessage(e).c_str(), exePath.c_str());

        if (capture.enabled) {
            // Close any inherited pipe ends we created (defensive cleanup)
            if (capture.hOutR) CloseHandle(capture.hOutR);
            if (capture.hOutW) CloseHandle(capture.hOutW);
            if (capture.hErrR) CloseHandle(capture.hErrR);
            if (capture.hErrW) CloseHandle(capture.hErrW);
        }

        if (e == ERROR_ELEVATION_REQUIRED || e == ERROR_ACCESS_DENIED) {
            Log(L"Attempting elevated relaunch via ShellExecuteExW(runas)...");
            if (TryElevatedLaunch(exePath, argsTail, workingDir, childExitCode)) {
                Log(L"Elevated launch completed with code %lu", (unsigned long)childExitCode);
                return true;
            }
        }

        std::wstring detail = L"Failed to start the game.\n\n"
                              L"Executable: " + exePath + L"\n\n" +
                              L"Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e) + L"\n\n"
                              L"See the launcher log for details:\n" +
                              (LogsRoot().empty() ? std::wstring(L"(unable to locate log directory)") :
                                                    LogsRoot() + L"\\launcher.log");
        FailBox(L"Launch Failed", detail);
    }

    // At this point the child is running. Start capture reader threads if enabled.
    StartCaptureThreadsIfNeeded(capture);

    // Optional: set process priority (best-effort)
    if (priorityClass != NORMAL_PRIORITY_CLASS) {
        if (!SetPriorityClass(pi.hProcess, priorityClass)) {
            Log(L"SetPriorityClass failed: %lu", GetLastError());
        } else {
            Log(L"Child priority set.");
        }
    }

    // Ensure child dies if the launcher dies
    if (gJob) {
        if (!AssignProcessToJobObject(gJob, pi.hProcess)) {
            Log(L"AssignProcessToJobObject failed: %lu", GetLastError());
        }
    }

    // Keep system & display awake while the game is running (optional)
    EXECUTION_STATE prevES = ES_CONTINUOUS;
    if (cfg.keepDisplayAwake) {
        prevES = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (cfg.keepDisplayAwake) {
        SetThreadExecutionState(ES_CONTINUOUS); // clear the flags
    }

    DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    // Ensure capture threads finish and close their handles.
    StopCaptureThreads(capture);

    childExitCode = exitCode;
    Log(L"Game exited with code %lu", (unsigned long)exitCode);

    // Friendly crash hint for common SEH codes
    if (exitCode >= 0xC0000000u) {
        std::wstring hint = L"";
        switch (exitCode) {
            case 0xC0000005u: hint = L"Access Violation (0xC0000005)"; break;
            case 0xC0000409u: hint = L"Stack Buffer Overrun / Fast Fail (0xC0000409)"; break;
            case 0xC000001Du: hint = L"Illegal Instruction (0xC000001D)"; break;
            default: break;
        }
        if (!hint.empty()) {
            Log(L"Crash hint: %s", hint.c_str());
        }
    }
    return true;
}

// -------------------------------------------------------------------------------------------------
// Utility: Decode SHIFT / CTRL pressed at launcher start to append special args
// -------------------------------------------------------------------------------------------------

void AppendConditionalArgs(std::vector<std::wstring>& merged, const LauncherConfig& cfg) {
    SHORT s = GetAsyncKeyState(VK_SHIFT);
    SHORT c = GetAsyncKeyState(VK_CONTROL);
    if ((s & 0x8000) && !cfg.safeArgs.empty()) {
        // Split cfg.safeArgs by spaces while respecting quotes? Keep simple: pass as one token.
        merged.emplace_back(cfg.safeArgs);
        Log(L"Safe args appended due to SHIFT: %s", cfg.safeArgs.c_str());
    }
    if ((c & 0x8000) && !cfg.repairArgs.empty()) {
        merged.emplace_back(cfg.repairArgs);
        Log(L"Repair args appended due to CTRL: %s", cfg.repairArgs.c_str());
    }
}

} // namespace

// -------------------------------------------------------------------------------------------------
// GUI subsystem entry point
// -------------------------------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // --- Bootstrap: DPI & Working Directory (very first!) ---
    win::EnablePerMonitorDpiAwareness();
    win::SetWorkingDirToExecutableDir();

    // Harden process a bit
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    // Legacy fallback; harmless after bootstrap (and still useful if manifest is absent).
    SetDpiAwareness();

    // Identify as an application for taskbar grouping & notifications (default; cfg may override)
    SetCurrentProcessExplicitAppUserModelID(L"ColonyGame.Launcher");

    gModuleDir = GetModuleDir();

    // Basic banner log (default location under LOCALAPPDATA until config overrides it)
    Log(L"=== Colony-Game Launcher v%s started ===", LAUNCHER_VERSION);
    Log(L"ModuleDir: %s", gModuleDir.c_str());

    // Load config (optional)
    LauncherConfig cfg{};
    ReadLauncherConfig(gModuleDir, cfg);

    // If config provided a custom AUMID, apply it now.
    if (!cfg.appUserModelID.empty()) {
        SetCurrentProcessExplicitAppUserModelID(cfg.appUserModelID.c_str());
    }

    // Configure logs root if requested
    if (cfg.portable) {
        SetLogCustomRoot(gModuleDir + L"\\logs");
        Log(L"Portable logging enabled -> %s", (gModuleDir + L"\\logs").c_str());
    } else if (!cfg.logsDir.empty()) {
        std::wstring lr = cfg.logsDir;
        if (PathIsRelativeW(lr.c_str())) lr = gModuleDir + L"\\" + lr;
        SetLogCustomRoot(lr);
        Log(L"Custom logs root -> %s", lr.c_str());
    }

    Log(L"AppUserModelID: %s", cfg.appUserModelID.c_str());

    // Single instance (Global so it also covers elevated/non-elevated mix)
    const std::wstring mutexName = cfg.mutexName.empty() ? L"Global\\ColonyGame_SingleInstance" : cfg.mutexName;
    gMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!gMutex) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error", L"CreateMutexW failed. Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        TryBringExistingToFront(cfg.bringTitleHint, gModuleDir);
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TASKMODAL);
        return 0;
    }

    // Working directory defaults to moduleDir first; refined below after we pick the EXE.
    if (!SetCurrentDirectoryW(gModuleDir.c_str())) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error",
                L"SetCurrentDirectoryW(\"" + gModuleDir + L"\") failed. Error " +
                std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }

    if (!SetupKillOnCloseJob()) {
        Log(L"Create JobObject (KILL_ON_JOB_CLOSE) failed: %lu", GetLastError());
    }

    // Find the game EXE (configured target takes precedence)
    std::wstring gameExe = FindGameExe(gModuleDir, cfg.target);
    if (gameExe.empty()) {
        FailBox(L"Launcher Error",
                L"Could not locate the game executable next to the launcher.\n\n"
                L"Create launcher.ini with a line like:\n    target=bin\\ColonyGame.exe");
    }
    const std::wstring exeDir = DirNameFromPath(gameExe);
    Log(L"Game EXE: %s", gameExe.c_str());

    // Determine desired working directory
    std::wstring workingDir;
    if (cfg.cwd.empty() || _wcsicmp(cfg.cwd.c_str(), L"auto") == 0) {
        workingDir = exeDir; // default: work from the game's directory
    } else if (_wcsicmp(cfg.cwd.c_str(), L"module_dir") == 0) {
        workingDir = gModuleDir;
    } else {
        std::wstring path = ExpandTokens(cfg.cwd, gModuleDir, exeDir);
        if (PathIsRelativeW(path.c_str())) workingDir = gModuleDir + L"\\" + path;
        else workingDir = path;
    }

    if (!workingDir.empty() && !SetCurrentDirectoryW(workingDir.c_str())) {
        DWORD e = GetLastError();
        Log(L"SetCurrentDirectoryW to workingDir failed (%lu: %s). Falling back to moduleDir.",
            e, GetLastErrorMessage(e).c_str());
        SetCurrentDirectoryW(gModuleDir.c_str());
        workingDir = gModuleDir;
    }
    Log(L"WorkingDir: %s", workingDir.c_str());

    // Ensure optional saves directory exists
    if (cfg.ensureSavesDir) {
        std::wstring sd = ExpandTokens(cfg.savesDir, gModuleDir, exeDir);
        EnsureDirRecursive(sd);
        SetEnvironmentVariableW(L"COLONY_SAVES_DIR", sd.c_str());
        Log(L"Saves Dir ensured: %s", sd.c_str());
    }

    // Optional: load env file (child inherits our environment)
    if (!cfg.envFile.empty()) {
        std::wstring envPath = cfg.envFile;
        if (PathIsRelativeW(envPath.c_str())) envPath = gModuleDir + L"\\" + envPath;
        if (Exists(envPath)) {
            LoadEnvFileAndApply(envPath, gModuleDir, exeDir);
        } else {
            Log(L"Env file not found (skipped): %s", envPath.c_str());
        }
    }

    // Provide a couple of useful env vars by default
    SetEnvironmentVariableW(L"COLONY_LAUNCHER_DIR", gModuleDir.c_str());
    SetEnvironmentVariableW(L"COLONY_GAME_DIR", exeDir.c_str());

    // Build the final argument tail (file args + user args)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::vector<std::wstring> fileArgs;
    if (!cfg.argsFile.empty()) {
        std::wstring argsPath = cfg.argsFile;
        if (PathIsRelativeW(argsPath.c_str())) argsPath = gModuleDir + L"\\" + argsPath;
        if (Exists(argsPath)) fileArgs = ReadArgsFile(argsPath);
        else Log(L"Args file not found (skipped): %s", argsPath.c_str());
    }

    std::vector<std::wstring> userArgs;
    if (argv) for (int i = 1; i < argc; ++i) userArgs.emplace_back(argv[i]);

    std::vector<std::wstring> merged;
    if (cfg.argsFilePrepend) {
        merged.insert(merged.end(), fileArgs.begin(), fileArgs.end());
        merged.insert(merged.end(), userArgs.begin(), userArgs.end());
    } else {
        merged.insert(merged.end(), userArgs.begin(), userArgs.end());
        merged.insert(merged.end(), fileArgs.begin(), fileArgs.end());
    }

    // Conditional safe/repair args (SHIFT/CTRL down)
    AppendConditionalArgs(merged, cfg);

    std::wstring argsTail = BuildArgsFromVector(merged);
    if (argv) LocalFree(argv);

    Log(L"Final args: %s", argsTail.c_str());

    // Best-effort: ensure VC++ redist (if configured and we can detect)
    MaybeEnsureVcRedist(cfg, gModuleDir, GetExeArch(gameExe));

    DWORD childExitCode = 0;
    const DWORD prio = PriorityClassFromString(cfg.priority);
    const bool ok = LaunchGame(gameExe, argsTail, workingDir, prio, cfg, childExitCode);

    if (gJob)   { CloseHandle(gJob);   gJob = nullptr; } // Will kill child if it's still running
    if (gMutex) { ReleaseMutex(gMutex); CloseHandle(gMutex); gMutex = nullptr; }

    Log(L"=== Launcher exiting (code %d) ===", ok ? (int)childExitCode : 1);
    return ok ? static_cast<int>(childExitCode) : 1;
}
