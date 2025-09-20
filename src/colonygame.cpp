// colonygame.cpp
// Windows-only, single-file colony game stub compatible with our Windows launcher.
// - No external dependencies (pure Win32 + GDI, Common Controls).
// - Accepts the same CLI/config flags used by the launcher: 
//   --config, --profile, --lang, --res WxH, --width, --height, --fullscreen, --vsync, --seed <n|random>, 
//   --safe-mode, --skip-intro, --validate
// - Writes/reads %APPDATA%\MarsColonySim\settings.ini, logs to %LOCALAPPDATA%\MarsColonySim\Logs
// - Returns 0 on --validate success, non-zero on failure.
//
// Build (MSVC dev prompt):
//   cl /EHsc /permissive- /W4 /DUNICODE /DWIN32_LEAN_AND_MEAN /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 colonygame.cpp ^
//      /link user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib comctl32.lib advapi32.lib ole32.lib
//
// Or use the CMakeLists.txt included at the end of this message.
//
// --------------------------------------------------------------------------------

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <objbase.h>
#include <Xinput.h> // XInput types/constants (we dynamically load the DLLs; no link lib required)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cassert>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <random>
#include <deque>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <cmath>    // for std::cos, std::copysign

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Enable v6 Common Controls visual styles without a .manifest
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

//======================================================================================
// Utilities
//======================================================================================
namespace util {

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring NowStampCompact() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring GetEnv(const wchar_t* name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768) return L"";
    return std::wstring(buf, n);
}

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool EnsureDir(const std::wstring& p) {
    if (DirExists(p)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS || DirExists(p);
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : path.substr(0, pos);
}

static std::wstring Quoted(const std::wstring& s) {
    if (s.find(L' ') != std::wstring::npos || s.find(L'\t') != std::wstring::npos) {
        return L"\"" + s + L"\"";
    }
    return s;
}

static void OpenInExplorer(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::wstring KnownFolderPath(REFKNOWNFOLDERID fid) {
    PWSTR p = nullptr;
    if (SHGetKnownFolderPath(fid, 0, nullptr, &p) != S_OK) return L"";
    std::wstring s = p; CoTaskMemFree(p);
    return s;
}

static std::wstring Format(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    wchar_t buf[4096]; _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap); va_end(ap);
    return buf;
}

template <typename T> static T clamp(T v, T lo, T hi) {
    return std::min(hi, std::max(lo, v));
}

} // namespace util

//======================================================================================
// Logging
//======================================================================================
class Logger {
public:
    bool Open(const std::wstring& logfile) {
        f_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        return f_.is_open();
    }
    void Line(const std::wstring& s) {
        if (!f_) return;
        auto t = util::NowStampCompact();
        std::wstring w = L"[" + t + L"] " + s + L"\r\n";
        // Correct wide-stream write: pass wchar_t* and count of wide chars.
        f_.write(w.c_str(), static_cast<std::streamsize>(w.size()));
        f_.flush();
    }
private:
    std::wofstream f_;
};

static Logger g_log;

//======================================================================================
// App paths / Config
//======================================================================================
static const wchar_t* kAppName = L"MarsColonySim";

struct AppPaths {
    std::wstring configDir;     // %APPDATA%\MarsColonySim
    std::wstring dataDir;       // %LOCALAPPDATA%\MarsColonySim
    std::wstring savesDir;      // dataDir\Saves
    std::wstring logsDir;       // dataDir\Logs
    std::wstring modsDir;       // dataDir\Mods
    std::wstring screenshotsDir;// dataDir\Screenshots
    std::wstring defaultConfig; // configDir\settings.ini
};

static AppPaths ComputePaths() {
    AppPaths p;
    std::wstring appdata  = util::GetEnv(L"APPDATA");
    std::wstring localapp = util::GetEnv(L"LOCALAPPDATA");
    if (appdata.empty())  appdata  = util::KnownFolderPath(FOLDERID_RoamingAppData);
    if (localapp.empty()) localapp = util::KnownFolderPath(FOLDERID_LocalAppData);
    p.configDir      = util::JoinPath(appdata,  kAppName);
    p.dataDir        = util::JoinPath(localapp, kAppName);
    p.savesDir       = util::JoinPath(p.dataDir, L"Saves");
    p.logsDir        = util::JoinPath(p.dataDir, L"Logs");
    p.modsDir        = util::JoinPath(p.dataDir, L"Mods");
    p.screenshotsDir = util::JoinPath(p.dataDir, L"Screenshots");
    p.defaultConfig  = util::JoinPath(p.configDir, L"settings.ini");
    util::EnsureDir(p.configDir);
    util::EnsureDir(p.dataDir);
    util::EnsureDir(p.savesDir);
    util::EnsureDir(p.logsDir);
    util::EnsureDir(p.modsDir);
    util::EnsureDir(p.screenshotsDir);
    return p;
}

struct Config {
    UINT width  = 1280;
    UINT height = 720;
    bool fullscreen = false;
    bool vsync      = true;
    bool skipIntro  = false;
    bool safeMode   = false;
    std::wstring profile = L"default";
    std::wstring lang    = L"en-US";
    std::optional<uint64_t> seed;
};

static std::wstring ReadFileW(const std::wstring& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return L"";
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Interpret config as UTF‑8
    return util::Widen(bytes);
}
static bool WriteFileW(const std::wstring& path, const std::wstring& content) {
    // Write config as UTF‑8 to match ReadFileW
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    std::string bytes = util::Narrow(content);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return true;
}
static void WriteDefaultConfig(const std::wstring& file, const Config& c) {
    std::wstringstream out;
    out << L"# Mars Colony Simulation - settings.ini\r\n"
        << L"# Windows game generated\r\n\r\n"
        << L"[Display]\r\n"
        << L"resolution=" << c.width << L"x" << c.height << L"\r\n"
        << L"fullscreen=" << (c.fullscreen ? L"true" : L"false") << L"\r\n"
        << L"vsync=" << (c.vsync ? L"true" : L"false") << L"\r\n\r\n"
        << L"[General]\r\n"
        << L"profile=" << c.profile << L"\r\n"
        << L"lang="    << c.lang    << L"\r\n\r\n"
        << L"[Startup]\r\n"
        << L"skip_intro=" << (c.skipIntro ? L"true" : L"false") << L"\r\n"
        << L"safe_mode="  << (c.safeMode  ? L"true" : L"false") << L"\r\n"
        << L"seed="       << (c.seed ? std::to_wstring(*c.seed) : L"") << L"\r\n";
    WriteFileW(file, out.str());
}
static bool ParseBool(const std::wstring& s, bool fallback=false) {
    std::wstring t = s; std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    if (t==L"1" || t==L"true" || t==L"yes" || t==L"on"  || t==L"enable" || t==L"enabled") return true;
    if (t==L"0" || t==L"false"|| t==L"no"  || t==L"off" || t==L"disable"|| t==L"disabled") return false;
    return fallback;
}
static std::optional<uint64_t> ParseU64(const std::wstring& s) {
    if (s.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    unsigned long long v = wcstoull(s.c_str(), &end, 10);
    if (!end || *end!=0) return std::nullopt;
    return (uint64_t)v;
}
static std::optional<std::pair<UINT,UINT>> ParseRes(const std::wstring& v) {
    size_t x = v.find(L'x');
    if (x == std::wstring::npos) return std::nullopt;
    UINT w = (UINT)_wtoi(v.substr(0, x).c_str());
    UINT h = (UINT)_wtoi(v.substr(x+1).c_str());
    if (w==0 || h==0) return std::nullopt;
    return std::make_pair(w,h);
}
static Config LoadConfig(const std::wstring& file, bool createIfMissing, const Config& defaults) {
    if (!util::FileExists(file)) {
        if (createIfMissing) WriteDefaultConfig(file, defaults);
        return defaults;
    }
    Config c = defaults;
    std::wistringstream in(ReadFileW(file));
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::wstring t = line;
        auto posc = t.find_first_of(L"#;/");
        if (posc != std::wstring::npos && (t[posc] == L'#' || t[posc]==L';' || (t[posc]==L'/' && posc+1<t.size() && t[posc+1]==L'/')))
            t = t.substr(0, posc);
        auto pos = t.find(L'=');
        if (pos == std::wstring::npos) continue;
        std::wstring key = t.substr(0,pos); std::wstring val = t.substr(pos+1);
        key.erase(0, key.find_first_not_of(L" \t\r\n")); key.erase(key.find_last_not_of(L" \t\r\n")+1);
        val.erase(0, val.find_first_not_of(L" \t\r\n")); val.erase(val.find_last_not_of(L" \t\r\n")+1);
        std::wstring k; k.resize(key.size());
        std::transform(key.begin(), key.end(), k.begin(), ::towlower);

        if (k == L"resolution") {
            if (auto r = ParseRes(val)) { c.width = r->first; c.height = r->second; }
        } else if (k == L"fullscreen") {
            c.fullscreen = ParseBool(val, c.fullscreen);
        } else if (k == L"vsync") {
            c.vsync = ParseBool(val, c.vsync);
        } else if (k == L"profile") {
            if (!val.empty()) c.profile = val;
        } else if (k == L"lang") {
            if (!val.empty()) c.lang = val;
        } else if (k == L"skip_intro") {
            c.skipIntro = ParseBool(val, c.skipIntro);
        } else if (k == L"safe_mode") {
            c.safeMode = ParseBool(val, c.safeMode);
        } else if (k == L"seed") {
            c.seed = ParseU64(val);
        }
    }
    return c;
}

//======================================================================================
// Windows Application Recovery & Restart (ARR)
// Register early so WER can call our recovery callback on crash/hang and restart us.
// Docs: RegisterApplicationRestart, RegisterApplicationRecoveryCallback, ApplicationRecoveryInProgress/Finished.
//======================================================================================
static DWORD WINAPI Colony_RecoveryCallback(PVOID /*ctx*/) {
    DWORD cancel = 0;
    ApplicationRecoveryInProgress(&cancel); // ping WER; returns cancel!=0 if user cancelled recovery
    if (cancel) {
        ApplicationRecoveryFinished(FALSE);
        return 0;
    }

    // Write a tiny recovery marker/autosave into a stable, per-user location:
    // %LOCALAPPDATA%\MarsColonySim\Recovery\autosave.json
    std::wstring base = util::KnownFolderPath(FOLDERID_LocalAppData);
    if (base.empty()) base = util::ExeDir();
    std::wstring dir  = util::JoinPath(util::JoinPath(base, kAppName), L"Recovery");
    util::EnsureDir(dir);
    std::wstring file = util::JoinPath(dir, L"autosave.json");

    const std::wstring jsonW = L"{\"recovered\":true,\"reason\":\"WER\",\"version\":1}\n";
    const std::string  bytes = util::Narrow(jsonW);

    HANDLE h = CreateFileW(file.c_str(),
                           GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                           nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0; (void)WriteFile(h, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
        FlushFileBuffers(h);
        CloseHandle(h);
    }

    ApplicationRecoveryFinished(TRUE); // signal success
    return 0;
}

static void InstallWindowsARR() {
    // Request OS to restart us after crash/hang/update with a recognizable flag.
    // Register before failure; WER restarts only if we've run ≥ 60 seconds.
    RegisterApplicationRestart(L"--restarted", 0);
    // Register recovery callback; ping interval 60s (we ping immediately in the callback).
    RegisterApplicationRecoveryCallback(&Colony_RecoveryCallback, nullptr, 60 * 1000, 0);
}

static bool WasRestartedByWer() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool restarted = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--restarted") == 0) { restarted = true; break; }
        }
        LocalFree(argv);
    }
    return restarted;
}

//======================================================================================
// XInput Dynamic Loader (no link dependency) + helpers
//======================================================================================
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);

static HMODULE            g_xiDLL            = nullptr;
static PFN_XInputGetState g_XInputGetState   = nullptr;
static PFN_XInputSetState g_XInputSetState   = nullptr;

static void LoadXInput()
{
    if (g_XInputGetState) return;
    const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" };
    for (auto* name : dlls) {
        g_xiDLL = LoadLibraryW(name);
        if (!g_xiDLL) continue;
        g_XInputGetState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(g_xiDLL, "XInputGetState"));
        g_XInputSetState = reinterpret_cast<PFN_XInputSetState>(GetProcAddress(g_xiDLL, "XInputSetState"));
        if (g_XInputGetState && g_XInputSetState) break;
        FreeLibrary(g_xiDLL); g_xiDLL = nullptr;
    }
}

static void UnloadXInput()
{
    g_XInputGetState = nullptr;
    g_XInputSetState = nullptr;
    if (g_xiDLL) { FreeLibrary(g_xiDLL); g_xiDLL = nullptr; }
}

// Normalize thumb value with proper dead-zone handling.
static float NormalizeThumb(SHORT v, SHORT deadzone)
{
    int iv = (int)v;
    int sign = (iv < 0) ? -1 : 1;
    int mag  = (iv < 0) ? -iv : iv;
    if (mag <= deadzone) return 0.0f;
    const float out = float(mag - deadzone) / float(32767 - deadzone);
    return sign * (out > 1.0f ? 1.0f : out);
}

//======================================================================================
// DPI: Enable Per‑Monitor v2 + helpers (graceful fallback for older systems)
//======================================================================================

// Fallback definitions for older SDKs where these are not present at compile-time.
#ifndef DPI_AWARENESS_CONTEXT
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static void EnablePerMonitorDpiV2()
{
    // Prefer Per‑Monitor v2 (Windows 10+) via SetProcessDpiAwarenessContext; fall back to SetProcessDPIAware.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetProcCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSet = reinterpret_cast<SetProcCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (pSet) {
            if (pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return; // success
            }
        }
    }
    // Oldest fallback — at least avoid bitmap stretching.
    SetProcessDPIAware();
}

//======================================================================================
// CLI parsing
//======================================================================================
struct LaunchOptions {
    std::optional<UINT> width;
    std::optional<UINT> height;
    std::optional<bool> fullscreen;
    std::optional<bool> vsync;
    std::optional<std::wstring> profile;
    std::optional<std::wstring> lang;
    std::optional<bool> skipIntro;
    std::optional<bool> safeMode;
    std::optional<uint64_t> seed;
    std::optional<std::wstring> configFile;
    bool validateOnly = false;
};

static bool StartsWith(const std::wstring& s, const std::wstring& p) {
    if (s.size() < p.size()) return false;
    return std::equal(p.begin(), p.end(), s.begin());
}
static std::optional<std::wstring> ValueOrNext(const std::vector<std::wstring>& args, size_t& i) {
    const auto& a = args[i];
    size_t eq = a.find(L'=');
    if (eq != std::wstring::npos) return a.substr(eq+1);
    if (i+1 < args.size()) {
        const auto& nxt = args[i+1];
        if (!(StartsWith(nxt, L"-") || StartsWith(nxt, L"--"))) { ++i; return nxt; }
    }
    return std::nullopt;
}
static bool ParseBoolFlag(const std::optional<std::wstring>& v, bool fallback) {
    if (!v.has_value()) return fallback;
    return ParseBool(*v, fallback);
}
static LaunchOptions ParseArgs(int argc, wchar_t** argv) {
    LaunchOptions opt;
    std::vector<std::wstring> args; args.reserve(argc);
    for (int i=0;i<argc;++i) args.push_back(argv[i]);

    for (size_t i=1;i<args.size();++i) {
        const auto& a = args[i];
        if (a == L"-h" || a == L"--help") {
            MessageBoxW(nullptr,
                L"Colony Game — Windows Build\n\n"
                L"Options:\n"
                L"  --config <file>\n"
                L"  --profile <name>\n"
                L"  --lang <code>\n"
                L"  --res <WxH>\n"
                L"  --width <px>\n"
                L"  --height <px>\n"
                L"  --fullscreen [true|false]\n"
                L"  --vsync [true|false]\n"
                L"  --seed <n|random>\n"
                L"  --safe-mode\n"
                L"  --skip-intro\n"
                L"  --validate\n", L"Help", MB_OK|MB_ICONINFORMATION);
            ExitProcess(0);
        } else if (a == L"--validate") {
            opt.validateOnly = true;
        } else if (StartsWith(a, L"--config")) {
            if (auto v = ValueOrNext(args, i)) opt.configFile = *v;
        } else if (StartsWith(a, L"--profile")) {
            if (auto v = ValueOrNext(args, i)) opt.profile = *v;
        } else if (StartsWith(a, L"--lang")) {
            if (auto v = ValueOrNext(args, i)) opt.lang = *v;
        } else if (StartsWith(a, L"--res")) {
            if (auto v = ValueOrNext(args, i)) {
                if (auto r = ParseRes(*v)) { opt.width = r->first; opt.height = r->second; }
            }
        } else if (StartsWith(a, L"--width")) {
            if (auto v = ValueOrNext(args, i)) { UINT w=(UINT)_wtoi(v->c_str()); if (w>0) opt.width=w; }
        } else if (StartsWith(a, L"--height")) {
            if (auto v = ValueOrNext(args, i)) { UINT h=(UINT)_wtoi(v->c_str()); if (h>0) opt.height=h; }
        } else if (StartsWith(a, L"--fullscreen")) {
            auto v = ValueOrNext(args, i); opt.fullscreen = ParseBoolFlag(v, true);
        } else if (StartsWith(a, L"--vsync")) {
            auto v = ValueOrNext(args, i); opt.vsync = ParseBoolFlag(v, true);
        } else if (StartsWith(a, L"--skip-intro")) {
            opt.skipIntro = true;
        } else if (StartsWith(a, L"--safe-mode")) {
            opt.safeMode = true;
        } else if (StartsWith(a, L"--seed")) {
            if (auto v = ValueOrNext(args, i)) {
                std::wstring s=*v; std::wstring tl=s; std::transform(tl.begin(),tl.end(),tl.begin(),::towlower);
                if (tl==L"random" || s.empty()) opt.seed.reset();
                else {
                    wchar_t* end=nullptr; unsigned long long val = wcstoull(s.c_str(), &end, 10);
                    if (end && *end==0) opt.seed = (uint64_t)val;
                }
            }
        }
    }
    return opt;
}
static Config MakeEffectiveConfig(const Config& file, const LaunchOptions& cli) {
    Config eff=file;
    if (cli.width) eff.width = *cli.width;
    if (cli.height) eff.height = *cli.height;
    if (cli.fullscreen) eff.fullscreen = *cli.fullscreen;
    if (cli.vsync) eff.vsync = *cli.vsync;
    if (cli.profile && !cli.profile->empty()) eff.profile = *cli.profile;
    if (cli.lang && !cli.lang->empty()) eff.lang = *cli.lang;
    if (cli.skipIntro) eff.skipIntro = *cli.skipIntro;
    if (cli.safeMode) eff.safeMode = *cli.safeMode;
    if (cli.seed.has_value()) eff.seed = cli.seed;
    return eff;
}

//======================================================================================
// Validate installation
//======================================================================================
static bool ValidateInstallation(std::wstring* messageOut=nullptr) {
    auto cwd = util::ExeDir();
    auto assets = util::JoinPath(cwd, L"assets");
    bool ok = util::DirExists(assets);
    if (!ok) {
        if (messageOut) *messageOut = L"assets\\ not found next to the executable.";
        return false;
    }
    // Light subfolders (non-fatal if missing, but warn)
    auto core = util::JoinPath(assets, L"core");
    auto locale = util::JoinPath(assets, L"locale");
    std::wstring dummy;
    if (!util::DirExists(core)) dummy += L"Missing assets\\core. ";
    if (!util::DirExists(locale)) dummy += L"Missing assets\\locale. ";
    if (messageOut) *messageOut = dummy;
    return true;
}

//======================================================================================
// High-resolution timing
//======================================================================================
class Timer {
public:
    Timer() { QueryPerformanceFrequency(&freq_); Reset(); }
    void Reset() { QueryPerformanceCounter(&last_); acc_=0.0; }
    double Tick() {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last_.QuadPart)/double(freq_.QuadPart);
        last_ = now;
        acc_ += dt;
        return dt;
    }
    double Accum() const { return acc_; }
    void ClearAcc() { acc_ = 0.0; }
private:
    LARGE_INTEGER freq_{}, last_{};
    double acc_ = 0.0;
};

//======================================================================================
// Simple RNG
//======================================================================================
class Rng {
public:
    explicit Rng(uint64_t seed) : eng_(seed) {}
    int irange(int lo, int hi) { std::uniform_int_distribution<int> d(lo,hi); return d(eng_); }
    bool chance(double p)      { std::bernoulli_distribution d(p); return d(eng_); }
    double frand(double a=0.0, double b=1.0) { std::uniform_real_distribution<double> d(a,b); return d(eng_); }
private:
    std::mt19937_64 eng_;
};

//======================================================================================
// World/Simulation (very similar to the SDL-based concept but rendered with GDI)
//======================================================================================
struct Vec2i { int x=0, y=0; bool operator==(const Vec2i& o) const { return x==o.x && y==o.y; } };
static inline Vec2i operator+(Vec2i a, Vec2i b){ return {a.x+b.x,a.y+b.y}; }
static inline Vec2i operator-(Vec2i a, Vec2i b){ return {a.x-b.x,a.y-b.y}; }
namespace std {
template<> struct hash<Vec2i> {
    size_t operator()(const Vec2i& v) const noexcept {
        return (uint64_t(uint32_t(v.x))<<32) ^ uint32_t(v.y);
    }
};
}

enum class TileType : uint8_t { Regolith=0, Rock=1, Ice=2, Crater=3, Sand=4 };
struct Tile {
    TileType type = TileType::Regolith;
    int resource = 0;
    bool walkable = true;
    uint8_t cost  = 10;
};
struct World {
    int W=120, H=80;
    std::vector<Tile> t;
    Rng* rng=nullptr;
    int idx(int x,int y) const { return y*W + x; }
    bool in(int x,int y) const { return x>=0 && y>=0 && x<W && y<H; }
    Tile& at(int x,int y){ return t[idx(x,y)]; }
    const Tile& at(int x,int y) const { return t[idx(x,y)]; }
    void resize(int w,int h){ W=w;H=h;t.assign(W*H,{}); }
    void generate(Rng& r){
        rng=&r;
        for(auto& e:t){ e.type=TileType::Regolith; e.resource=0; e.walkable=true; e.cost=10; }
        // Sand swirls
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            if(r.chance(0.015)){
                int len=r.irange(8,30), dx= (int)std::copysign(1.0, r.frand(-1,1)), dy=(int)std::copysign(1.0, r.frand(-1,1));
                int cx=x,cy=y;
                for(int i=0;i<len;++i){ if(!in(cx,cy)) break; auto& tt=at(cx,cy); tt.type=TileType::Sand; tt.cost=12; cx+=dx; cy+=dy; }
            }
        }
        // Ice pockets
        for(int k=0;k<180;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,2)){ auto& tt=at(X,Y); tt.type=TileType::Ice; tt.walkable=true; tt.cost=14; tt.resource=r.irange(5,20); }
            }
        }
        // Rock clusters
        for(int k=0;k<220;++k){
            int x=r.irange(0,W-1), y=r.irange(0,H-1), R=r.irange(2,5);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-2,2)){ auto& tt=at(X,Y); tt.type=TileType::Rock; tt.walkable=true; tt.cost=16; tt.resource=r.irange(3,12); }
            }
        }
        // Craters
        for(int k=0;k<55;++k){
            int x=r.irange(4,W-5), y=r.irange(4,H-5), R=r.irange(2,4);
            for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){
                int X=x+dx,Y=y+dy; if(!in(X,Y)) continue;
                if(dx*dx+dy*dy<=R*R + r.irange(-1,1)){ auto& tt=at(X,Y); tt.type=TileType::Crater; tt.walkable=false; tt.cost=255; tt.resource=0; }
            }
        }
        // HQ area
        int cx=W/2, cy=H/2;
        for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){
            int X=cx+dx,Y=cy+dy; if(!in(X,Y)) continue; auto& tt=at(X,Y); tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
        }
    }
};

// Pathfinding A* (4-neigh)
static int manhattan(Vec2i a, Vec2i b){ return std::abs(a.x-b.x)+std::abs(a.y-b.y); }
static bool neighbors(const World& w, const Vec2i& p, std::array<Vec2i,4>& out, int& n){
    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
    n=0; for(auto d:N){ int nx=p.x+d.x, ny=p.y+d.y; if(!w.in(nx,ny)) continue; if(!w.at(nx,ny).walkable) continue; out[n++]={nx,ny}; } return n>0;
}
static bool findPath(const World& w, Vec2i start, Vec2i goal, std::deque<Vec2i>& out) {
    if(!w.in(start.x,start.y)||!w.in(goal.x,goal.y)) return false;
    if(!w.at(start.x,start.y).walkable||!w.at(goal.x,goal.y).walkable) return false;

    struct Node{ Vec2i p; int g=0,f=0,parent=-1; };
    struct PQ{ int idx; int f; bool operator<(const PQ& o) const { return f>o.f; } };

    auto idxOf=[&](Vec2i p){ return p.y*w.W+p.x; };
    std::vector<Node> nodes; nodes.reserve(w.W*w.H);
    std::vector<int> openIx(w.W*w.H,-1), closedIx(w.W*w.H,-1);
    std::priority_queue<PQ> open;

    Node s; s.p=start; s.g=0; s.f=manhattan(start,goal); s.parent=-1;
    nodes.push_back(s); open.push({0,s.f}); openIx[idxOf(start)]=0;

    std::array<Vec2i,4> neigh; int nc=0;
    while(!open.empty()){
        int ci=open.top().idx; open.pop();
        Node cur=nodes[ci]; Vec2i p=cur.p;
        if(p==goal){
            std::vector<Vec2i> rev; for(int i=ci;i!=-1;i=nodes[i].parent) rev.push_back(nodes[i].p);
            out.clear(); for(int i=(int)rev.size()-1;i>=0;--i) out.push_back(rev[i]);
            if(!out.empty()) out.pop_front();
            return true;
        }
        closedIx[idxOf(p)]=ci;

        neighbors(w,p,neigh,nc);
        for(int i=0;i<nc;++i){
            Vec2i np=neigh[i]; int nid=idxOf(np);
            if(closedIx[nid]!=-1) continue;
            int step=w.at(np.x,np.y).cost; int g=cur.g+step;
            int o=openIx[nid];
            if(o==-1){
                Node n; n.p=np; n.g=g; n.f=g+manhattan(np,goal); n.parent=ci;
                o=(int)nodes.size(); nodes.push_back(n); open.push({o,n.f}); openIx[nid]=o;
            } else if(g < nodes[o].g){
                nodes[o].g=g; nodes[o].f=g+manhattan(np,goal); nodes[o].parent=ci; open.push({o,nodes[o].f});
            }
        }
    }
    return false;
}

// Colony economy and entities
enum class Resource : uint8_t { Metal=0, Ice=1, Oxygen=2, Water=3 };
struct Stockpile { int metal=15, ice=10, oxygen=50, water=40; };

enum class BuildingKind : uint8_t { Solar=0, Habitat=1, OxyGen=2 };
struct BuildingDef {
    BuildingKind kind; Vec2i size;
    int metalCost=0, iceCost=0;
    int powerProd=0, powerCons=0;
    int oxyProd=0,  oxyCons=0;
    int waterProd=0, waterCons=0;
    int housing=0; bool needsDaylight=false;
};
static BuildingDef defSolar()  { return {BuildingKind::Solar,  {2,2},  6,0,   8,0, 0,0, 0,0, 0, true}; }
static BuildingDef defHab()    { return {BuildingKind::Habitat,{3,2}, 12,4,   0,2, 0,2, 0,2, 4, false}; }
static BuildingDef defOxyGen() { return {BuildingKind::OxyGen, {2,2}, 10,6,   2,0, 4,0, 0,0, 0, false}; }

struct Building {
    int id=0; BuildingDef def; Vec2i pos; bool powered=true;
};
struct Colony {
    Stockpile store;
    int powerBalance=0, oxygenBalance=0, waterBalance=0;
    int housing=0, population=0;
};

enum class JobType : uint8_t { None=0, MineRock=1, MineIce=2, Deliver=3, Build=4 };
struct Job { JobType type=JobType::None; Vec2i target{}; int ticks=0; int amount=0; int buildingId=0; };
struct Colonist {
    int id=0; Vec2i tile{0,0}; std::deque<Vec2i> path; Job job; int carryMetal=0; int carryIce=0;
    enum class State: uint8_t { Idle, Moving, Working } state=State::Idle;
};

//======================================================================================
// GDI Rendering helpers (double buffer)
//======================================================================================
struct BackBuffer {
    HBITMAP bmp = 0;
    HDC     mem = 0;
    int     w=0, h=0;
    void Create(HDC hdc, int W, int H) {
        Destroy();
        w=W; h=H;
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, W, H);
        SelectObject(mem, bmp);
        HBRUSH b = CreateSolidBrush(RGB(0,0,0));
        RECT rc{0,0,W,H}; FillRect(mem, &rc, b); DeleteObject(b);
    }
    void Destroy() {
        if (mem) { DeleteDC(mem); mem=0; }
        if (bmp) { DeleteObject(bmp); bmp=0; }
        w=h=0;
    }
};

//======================================================================================
// Game application (Win32)
//======================================================================================
static const wchar_t* kWndClass = L"ColonyGame_Win32_Class";
static const wchar_t* kWndTitle = L"Colony Game (Win32)";

class GameApp {
public:
    GameApp(HINSTANCE hInst, const AppPaths& paths, const Config& cfg)
        : hInst_(hInst), paths_(paths), cfg_(cfg),
          // Robust RNG seeding: remove invalid literal suffix and use a solid default
          rng_(cfg.seed.value_or(0x9E3779B97F4A7C15ull)) {}

    int Run() {
        if (!CreateMainWindow()) return 3;
        InitWorld();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        // Initialize XInput (dynamic loader + first-pad discovery)
        InitGamepad();

        Timer timer;
        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running_=false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            double dt = timer.Tick();

            // Poll controller once per frame (affects camera pan/zoom and actions)
            PollGamepad(dt);

            if (!paused_) {
                simAcc_ += dt * simSpeed_;
                if (simAcc_ > 0.5) simAcc_ = 0.5;
                while (simAcc_ >= fixedDt_) {
                    Update(fixedDt_);
                    simAcc_ -= fixedDt_;
                }
            }

            Render();
            if (cfg_.vsync) { // crude vsync-ish
                Sleep(1);
            }
        }
        return 0;
    }

private:
    // ------------------ Window / WndProc ------------------
    static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        GameApp* self = reinterpret_cast<GameApp*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(h, m, w, l);
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->WndProc(h, m, w, l);
    }

    bool CreateMainWindow() {
        WNDCLASSW wc{}; wc.hInstance=hInst_; wc.lpfnWndProc=StaticWndProc;
        wc.hCursor=LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=kWndClass;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        if (!RegisterClassW(&wc)) return false;

        DWORD style = WS_OVERLAPPEDWINDOW;
        if (cfg_.fullscreen) style = WS_POPUP;

        RECT rc{0,0,(LONG)cfg_.width,(LONG)cfg_.height};
        AdjustWindowRect(&rc, style, FALSE);
        int W = rc.right-rc.left, H = rc.bottom-rc.top;

        hwnd_ = CreateWindowExW(0, kWndClass, kWndTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT, W, H, nullptr, nullptr, hInst_, this);
        if (!hwnd_) return false;

        // DPI-aware font for HUD using the window's current DPI
        UINT dpi = 0;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
            auto pGet = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            if (pGet) dpi = pGet(hwnd_); // Win10+ API
        }
        dpi_ = (dpi != 0) ? dpi : 96;
        LOGFONTW lf{}; lf.lfHeight = -MulDiv(10, (int)dpi_, 96);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf);

        return true;
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_DPICHANGED: {
            // Use the suggested rectangle (MS guidance) to reposition/resize at the new DPI. :contentReference[oaicite:1]{index=1}
            const RECT* suggested = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            // Update cached DPI and rebuild HUD font at the new DPI.
            UINT dpiX = LOWORD(w); UINT dpiY = HIWORD(w);
            dpi_ = (dpiY != 0) ? dpiY : dpi_;
            if (font_) { DeleteObject(font_); font_ = nullptr; }
            LOGFONTW lf{}; lf.lfHeight = -MulDiv(10, (int)dpi_, 96);
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            font_ = CreateFontIndirectW(&lf);
            return 0;
        }
        case WM_SIZE: {
            clientW_ = LOWORD(l); clientH_ = HIWORD(l);
            HDC hdc = GetDC(h);
            if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);
            ReleaseDC(h, hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx=GET_X_LPARAM(l), my=GET_Y_LPARAM(l);
            OnLeftClick(mx,my);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            buildMode_ = false; selected_ = std::nullopt;
            return 0;
        }
        case WM_MOUSEWHEEL: {
            short z = GET_WHEEL_DELTA_WPARAM(w);
            if (z>0) zoom_ = util::clamp(zoom_ * 1.1, 0.5, 2.5);
            else     zoom_ = util::clamp(zoom_ / 1.1, 0.5, 2.5);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (w) {
                case VK_ESCAPE:
                    if (buildMode_) { buildMode_=false; selected_.reset(); }
                    else { running_=false; }
                    break;
                case 'P': paused_ = !paused_; break;
                case VK_OEM_PLUS: case VK_ADD: simSpeed_=util::clamp(simSpeed_*1.25,0.25,8.0); break;
                case VK_OEM_MINUS: case VK_SUBTRACT: simSpeed_=util::clamp(simSpeed_/1.25,0.25,8.0); break;
                case '1': selected_=BuildingKind::Solar;  buildMode_=true;  break;
                case '2': selected_=BuildingKind::Habitat;buildMode_=true;  break;
                case '3': selected_=BuildingKind::OxyGen; buildMode_=true;  break;
                case 'G': SpawnColonist(); break;
                case 'B': { auto t=MouseToTile(lastMouse_); Bulldoze(t); } break;
                case VK_LEFT:  keyPan_.x=-1; break;
                case VK_RIGHT: keyPan_.x=+1; break;
                case VK_UP:    keyPan_.y=-1; break;
                case VK_DOWN:  keyPan_.y=+1; break;
            }
            return 0;
        }
        case WM_KEYUP: {
            switch (w) {
                case VK_LEFT:  if (keyPan_.x==-1) keyPan_.x=0; break;
                case VK_RIGHT: if (keyPan_.x==+1) keyPan_.x=0; break;
                case VK_UP:    if (keyPan_.y==-1) keyPan_.y=0; break;
                case VK_DOWN:  if (keyPan_.y==+1) keyPan_.y=0; break;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            lastMouse_.x = GET_X_LPARAM(l);
            lastMouse_.y = GET_Y_LPARAM(l);
            return 0;
        }
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    // ------------------ Gamepad (XInput) ------------------
    void InitGamepad()
    {
        LoadXInput();
        if (!g_XInputGetState) { padConnected_ = false; return; }
        for (DWORD i = 0; i < 4; ++i) {
            XINPUT_STATE st{};
            if (g_XInputGetState(i, &st) == ERROR_SUCCESS) {
                padIndex_ = i; padConnected_ = true; padPrev_ = st; break;
            }
        }
    }
    void SetRumble(float seconds, WORD left = 30000, WORD right = 0)
    {
        if (!padConnected_ || !g_XInputSetState) return;
        XINPUT_VIBRATION vib{ left, right };
        g_XInputSetState(padIndex_, &vib);
        rumbleUntil_ = seconds;
    }
    bool WasPressed(const XINPUT_STATE& now, WORD buttonMask)
    {
        const WORD was = padPrev_.Gamepad.wButtons & buttonMask;
        const WORD is  = now.Gamepad.wButtons & buttonMask;
        return !was && is;
    }
    void PollGamepad(double dt)
    {
        if (!g_XInputGetState) return;

        XINPUT_STATE st{};
        if (g_XInputGetState(padIndex_, &st) != ERROR_SUCCESS) {
            padConnected_ = false;
            for (DWORD i = 0; i < 4; ++i) {
                if (g_XInputGetState(i, &st) == ERROR_SUCCESS) { padIndex_ = i; padConnected_ = true; break; }
            }
            if (!padConnected_) return;
        }

        // Left stick pan (with dead-zone); Y inverted for screen space
        const float lx = NormalizeThumb(st.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const float ly = NormalizeThumb(st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        padPanX_ = lx;
        padPanY_ = -ly;

        // Triggers zoom
        const bool lt = st.Gamepad.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        const bool rt = st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        if (lt || rt) {
            double z = zoom_;
            if (rt) z = util::clamp(z * (1.0 + 0.75 * dt), 0.5, 2.5);
            if (lt) z = util::clamp(z * (1.0 - 0.75 * dt), 0.5, 2.5);
            zoom_ = z;
        }

        // A place (if in build mode), B cancel, X bulldoze, Y spawn colonist
        if (WasPressed(st, XINPUT_GAMEPAD_A) && buildMode_ && selected_.has_value()) {
            Vec2i t = MouseToTile(lastMouse_);
            if (TryQueueBuild(*selected_, t)) {
                SetRumble(0.15f, 25000, 0);
                buildMode_ = false; selected_.reset();
            } else {
                SetRumble(0.10f, 12000, 0);
            }
        }
        if (WasPressed(st, XINPUT_GAMEPAD_B) && buildMode_) {
            buildMode_ = false; selected_.reset();
        }
        if (WasPressed(st, XINPUT_GAMEPAD_X)) {
            auto t = MouseToTile(lastMouse_);
            Bulldoze(t);
            SetRumble(0.08f, 18000, 0);
        }
        if (WasPressed(st, XINPUT_GAMEPAD_Y)) {
            SpawnColonist();
            SetRumble(0.08f, 20000, 0);
        }

        // LB/RB quick-select
        if (WasPressed(st, XINPUT_GAMEPAD_LEFT_SHOULDER))  { selected_ = BuildingKind::Solar;  buildMode_ = true; }
        if (WasPressed(st, XINPUT_GAMEPAD_RIGHT_SHOULDER)) { selected_ = BuildingKind::Habitat;buildMode_ = true; }

        // DPad adjust sim speed; Start pause
        if (WasPressed(st, XINPUT_GAMEPAD_DPAD_UP))   simSpeed_ = util::clamp(simSpeed_ * 1.25, 0.25, 8.0);
        if (WasPressed(st, XINPUT_GAMEPAD_DPAD_DOWN)) simSpeed_ = util::clamp(simSpeed_ / 1.25, 0.25, 8.0);
        if (WasPressed(st, XINPUT_GAMEPAD_START))     paused_ = !paused_;

        // Rumble timeout
        if (rumbleUntil_ > 0.0) {
            rumbleUntil_ -= dt;
            if (rumbleUntil_ <= 0.0 && g_XInputSetState) {
                XINPUT_VIBRATION vib{ 0, 0 };
                g_XInputSetState(padIndex_, &vib);
            }
        }

        padPrev_ = st;
    }

    // ------------------ World / Sim init ------------------
    void InitWorld() {
        tileSize_ = 24;
        world_.resize(120, 80);
        world_.generate(rng_);

        hq_ = { world_.W/2, world_.H/2 };
        TryPlaceImmediate(BuildingKind::Solar,  hq_ + Vec2i{3,-2});
        TryPlaceImmediate(BuildingKind::Habitat,hq_ + Vec2i{3, 0});
        TryPlaceImmediate(BuildingKind::OxyGen, hq_ + Vec2i{0, 3});

        camera_.x = (hq_.x*tileSize_) - clientW_/2;
        camera_.y = (hq_.y*tileSize_) - clientH_/2;

        SpawnColonist();
    }

    void SpawnColonist() {
        Colonist c; c.id = nextColonistId_++; c.tile = hq_; colonists_.push_back(c);
        Banner(L"Colonist arrived");
    }

    // ------------------ Input helpers ------------------
    Vec2i MouseToTile(POINT p) const {
        int wx = int(camera_.x + p.x/zoom_);
        int wy = int(camera_.y + p.y/zoom_);
        return { wx / tileSize_, wy / tileSize_ };
    }
    void OnLeftClick(int mx, int my) {
        POINT p{mx,my};
        if (buildMode_ && selected_.has_value()) {
            Vec2i t = MouseToTile(p);
            TryQueueBuild(*selected_, t);
            buildMode_=false; selected_.reset();
            return;
        }
    }

    // ------------------ Build placement ------------------
    BuildingDef Def(BuildingKind k) {
        switch(k) {
            case BuildingKind::Solar:  return defSolar();
            case BuildingKind::Habitat:return defHab();
            case BuildingKind::OxyGen: return defOxyGen();
        }
        return defSolar();
    }
    bool CheckFootprint(const BuildingDef& d, Vec2i topLeft) {
        for(int dy=0;dy<d.size.y;++dy) for(int dx=0;dx<d.size.x;++dx) {
            int x=topLeft.x+dx, y=topLeft.y+dy;
            if(!world_.in(x,y)) return false;
            const Tile& t = world_.at(x,y);
            if(!t.walkable || t.type==TileType::Crater) return false;
        }
        return true;
    }
    void Bulldoze(Vec2i t) {
        if(!world_.in(t.x,t.y)) return;
        auto& tt=world_.at(t.x,t.y);
        tt.type=TileType::Regolith; tt.walkable=true; tt.cost=10; tt.resource=0;
    }
    bool TryQueueBuild(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) { Banner(L"Invalid location"); return false; }
        if (colony_.store.metal < d.metalCost || colony_.store.ice < d.iceCost) { Banner(L"Not enough resources"); return false; }
        pendingBuild_ = Building{ nextBuildingId_++, d, topLeft, true };
        Banner(L"Construction queued: " + NameOf(k));
        return true;
    }
    void TryPlaceImmediate(BuildingKind k, Vec2i topLeft) {
        BuildingDef d = Def(k);
        if (!CheckFootprint(d, topLeft)) return;
        buildings_.push_back(Building{ nextBuildingId_++, d, topLeft, true });
    }

    // ------------------ Update loop ------------------
    void Update(double dt) {
        // Camera pan (keyboard)
        const double pan=300.0;
        camera_.x += keyPan_.x * pan * dt;
        camera_.y += keyPan_.y * pan * dt;
        // Camera pan (gamepad analog)
        camera_.x += padPanX_ * pan * dt;
        camera_.y += padPanY_ * pan * dt;

        // Day/night
        dayTime_ += dt*0.02;
        if (dayTime_>=1.0) dayTime_ -= 1.0;

        EconomyTick();
        AITick();
    }
    void EconomyTick() {
        colony_.powerBalance = colony_.oxygenBalance = colony_.waterBalance = 0;
        colony_.housing = 0;
        bool daylight = (dayTime_>0.1 && dayTime_<0.9);
        for(auto& b:buildings_) {
            b.powered = true;
            if (b.def.needsDaylight && !daylight) { /* no production */ }
            else colony_.powerBalance += b.def.powerProd;
            colony_.powerBalance -= b.def.powerCons;

            colony_.oxygenBalance += b.def.oxyProd;
            colony_.oxygenBalance -= b.def.oxyCons;
            colony_.waterBalance  += b.def.waterProd;
            colony_.waterBalance  -= b.def.waterCons;
            colony_.housing       += b.def.housing;
        }
        colony_.store.oxygen = std::max(0, colony_.store.oxygen + colony_.oxygenBalance);
        colony_.store.water  = std::max(0, colony_.store.water + colony_.waterBalance);
        int people = (int)colonists_.size();
        if (people>0) {
            colony_.store.oxygen = std::max(0, colony_.store.oxygen - people);
            colony_.store.water  = std::max(0, colony_.store.water  - people);
        }
        colony_.population=people;

        // If pending build and enough resources, "assign" when a colonist arrives
    }
    void AITick() {
        for (auto& c:colonists_) {
            switch (c.state) {
                case Colonist::State::Idle:    AIIdle(c); break;
                case Colonist::State::Moving:  AIMove(c); break;
                case Colonist::State::Working: AIWork(c); break;
            }
        }
    }
    void AIIdle(Colonist& c) {
        if (pendingBuild_.has_value()) {
            // Go adjacent to footprint
            std::vector<Vec2i> opts;
            for(int dy=0;dy<pendingBuild_->def.size.y;++dy)
                for(int dx=0;dx<pendingBuild_->def.size.x;++dx) {
                    Vec2i p = pendingBuild_->pos + Vec2i{dx,dy};
                    static const std::array<Vec2i,4> N={{ {1,0},{-1,0},{0,1},{0,-1} }};
                    for(auto d:N){ Vec2i n=p+d; if(world_.in(n.x,n.y)&&world_.at(n.x,n.y).walkable) opts.push_back(n); }
                }
            if(!opts.empty()){
                Vec2i pick = opts[rng_.irange(0,(int)opts.size()-1)];
                std::deque<Vec2i> path;
                if (findPath(world_, c.tile, pick, path)) {
                    c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Build,pendingBuild_->pos,18,0,pendingBuild_->id};
                    return;
                }
            }
        }
        if (colony_.store.oxygen < 40) if (TryAssignMining(c, TileType::Ice)) return;
        if (TryAssignMining(c, TileType::Rock)) return;
        // wander to HQ
        if (c.tile != hq_) {
            std::deque<Vec2i> path; if(findPath(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,0,0};}
        }
    }
    bool TryAssignMining(Colonist& c, TileType tt) {
        int bestD = INT32_MAX; Vec2i best{-1,-1};
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x){
            const Tile& t=world_.at(x,y);
            if (t.type==tt && t.resource>0 && t.walkable) {
                int d=manhattan(c.tile,{x,y});
                if(d<bestD){bestD=d; best={x,y};}
            }
        }
        if (best.x>=0) {
            std::deque<Vec2i> path; if(findPath(world_, c.tile, best, path)) {
                c.path=std::move(path); c.state=Colonist::State::Moving;
                c.job={ (tt==TileType::Ice)?JobType::MineIce:JobType::MineRock, best, 18, 0, 0 };
                return true;
            }
        }
        return false;
    }
    void AIMove(Colonist& c) {
        moveAcc_ += fixedDt_;
        const double step=0.12;
        if (moveAcc_>=step && !c.path.empty()) {
            c.tile=c.path.front(); c.path.pop_front();
            moveAcc_-=step;
            if (c.path.empty()) { c.state=Colonist::State::Working; c.job.ticks=18; }
        }
    }
    void AIWork(Colonist& c) {
        if (c.job.ticks>0) { --c.job.ticks; return; }
        if (c.job.type==JobType::MineIce || c.job.type==JobType::MineRock) {
            Tile& t=world_.at(c.job.target.x,c.job.target.y);
            int mined = std::min(3, t.resource);
            if (mined<=0){ c.state=Colonist::State::Idle; return; }
            t.resource -= mined;
            if (c.job.type==JobType::MineIce) c.carryIce += mined; else c.carryMetal += mined;
            std::deque<Vec2i> path; if(findPath(world_, c.tile, hq_, path)){c.path=std::move(path); c.state=Colonist::State::Moving; c.job={JobType::Deliver,hq_,0,mined,0};}
            else c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Deliver) {
            colony_.store.metal += c.carryMetal; c.carryMetal=0;
            colony_.store.ice   += c.carryIce;   c.carryIce=0;
            c.state=Colonist::State::Idle;
        } else if (c.job.type==JobType::Build) {
            if (pendingBuild_.has_value() && pendingBuild_->id==c.job.buildingId) {
                if (colony_.store.metal >= pendingBuild_->def.metalCost &&
                    colony_.store.ice   >= pendingBuild_->def.iceCost) {
                    colony_.store.metal -= pendingBuild_->def.metalCost;
                    colony_.store.ice   -= pendingBuild_->def.iceCost;
                    buildings_.push_back(*pendingBuild_);
                    pendingBuild_.reset();
                }
            }
            c.state=Colonist::State::Idle;
        } else {
            c.state=Colonist::State::Idle;
        }
    }

    // ------------------ Rendering ------------------
    void Render() {
        HDC hdc = GetDC(hwnd_);
        if (!back_.mem || back_.w!=clientW_ || back_.h!=clientH_) back_.Create(hdc, clientW_, clientH_);

        // Mars-ish sky based on dayTime_
        double daylight = std::cos((dayTime_-0.5)*3.14159*2.0)*0.5+0.5;
        int R=int(120+70*daylight), G=int(40+30*daylight), B=int(35+25*daylight);
        HBRUSH sky = CreateSolidBrush(RGB(R,G,B));
        RECT full{0,0,clientW_,clientH_}; FillRect(back_.mem, &full, sky); DeleteObject(sky);

        DrawWorld();
        DrawBuildings();
        DrawColonists();
        if (buildMode_ && selected_.has_value()) DrawPlacement(*selected_);
        DrawHQ();
        DrawHUD();

        BitBlt(hdc, 0,0, clientW_,clientH_, back_.mem, 0,0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);
    }

    void DrawWorld() {
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x) {
            const Tile& t=world_.at(x,y);
            COLORREF c = RGB(139,85,70);
            switch(t.type){
                case TileType::Regolith: c=RGB(139,85,70); break;
                case TileType::Sand:     c=RGB(168,120,85); break;
                case TileType::Ice:      c=RGB(120,170,200); break;
                case TileType::Rock:     c=RGB(100,100,110); break;
                case TileType::Crater:   c=RGB(40,40,45); break;
            }
            DrawCell(x,y,c);
            // grid line (subtle)
            HPEN pen=CreatePen(PS_SOLID,1,RGB(0,0,0)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
            RECT rc = TileRect(x,y); MoveToEx(back_.mem, rc.left, rc.top, nullptr);
            LineTo(back_.mem, rc.right, rc.top); LineTo(back_.mem, rc.right, rc.bottom); LineTo(back_.mem, rc.left, rc.bottom); LineTo(back_.mem, rc.left, rc.top);
            SelectObject(back_.mem, old); DeleteObject(pen);
        }
    }
    void DrawBuildings() {
        for(auto& b:buildings_){
            COLORREF col = (b.def.kind==BuildingKind::Solar)?RGB(60,120,200):
                           (b.def.kind==BuildingKind::Habitat)?RGB(200,160,80):RGB(90,200,140);
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(col); FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        if (pendingBuild_.has_value()) {
            auto& b=*pendingBuild_;
            RECT rc = TileRect(b.pos.x,b.pos.y);
            rc.right  = rc.left + int(b.def.size.x * tileSize_ * zoom_);
            rc.bottom = rc.top  + int(b.def.size.y * tileSize_ * zoom_);
            HBRUSH br=CreateSolidBrush(RGB(255,255,255)); // translucent-ish: simulate with hatch
            FillRect(back_.mem,&rc,br); DeleteObject(br);
            FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
    }
    void DrawColonists() {
        for(auto& c:colonists_) {
            RECT rc = TileRect(c.tile.x,c.tile.y);
            HBRUSH br=CreateSolidBrush(RGB(240,90,70)); FillRect(back_.mem,&rc,br); DeleteObject(br);

            if (!c.path.empty()) {
                HPEN pen=CreatePen(PS_SOLID,2,RGB(30,220,255)); HPEN old=(HPEN)SelectObject(back_.mem, pen);
                Vec2i prev=c.tile;
                for(auto p:c.path){
                    RECT a=TileRect(prev.x,prev.y), b=TileRect(p.x,p.y);
                    int ax=(a.left+a.right)/2, ay=(a.top+a.bottom)/2;
                    int bx=(b.left+b.right)/2, by=(b.top+b.bottom)/2;
                    MoveToEx(back_.mem, ax, ay, nullptr); LineTo(back_.mem, bx, by);
                    prev=p;
                }
                SelectObject(back_.mem, old); DeleteObject(pen);
            }
        }
    }
    void DrawPlacement(BuildingKind k) {
        Vec2i t = MouseToTile(lastMouse_);
        auto d = Def(k);
        bool ok = CheckFootprint(d,t);
        RECT rc = TileRect(t.x,t.y);
        rc.right  = rc.left + int(d.size.x * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(d.size.y * tileSize_ * zoom_);
        HBRUSH br = CreateSolidBrush(ok?RGB(100,255,100):RGB(255,80,80));
        FillRect(back_.mem,&rc,br); DeleteObject(br);
        FrameRect(back_.mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        std::wstringstream tip;
        tip << NameOf(k) << L"  M:" << d.metalCost << L" I:" << d.iceCost;
        DrawTooltip(lastMouse_.x+14, lastMouse_.y+14, tip.str());
    }
    void DrawHQ() {
        RECT rc = TileRect(hq_.x,hq_.y);
        rc.right  = rc.left + int(2 * tileSize_ * zoom_);
        rc.bottom = rc.top  + int(2 * tileSize_ * zoom_);
        HBRUSH br=CreateSolidBrush(RGB(200,80,120)); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    void DrawHUD() {
        int pad=8, w=540, h=110;
        RECT hud{pad,pad,pad+w,pad+h};
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&hud,bg); DeleteObject(bg);
        FrameRect(back_.mem,&hud,(HBRUSH)GetStockObject(BLACK_BRUSH));

        HFONT oldFont = (HFONT)SelectObject(back_.mem, font_);
        SetBkMode(back_.mem, TRANSPARENT);
        SetTextColor(back_.mem, RGB(230,230,240));

        int x=hud.left+8, y=hud.top+6;
        std::wstringstream l1; l1<<L"Time "<<std::fixed<<std::setprecision(2)<<dayTime_<<L"   x"<<std::setprecision(2)<<simSpeed_<<(paused_?L"  [PAUSED]":L"");
        DrawTextLine(x,y,l1.str()); y+=16;

        std::wstringstream r1; r1<<L"Metal "<<colony_.store.metal<<L"   Ice "<<colony_.store.ice<<L"   O2 "<<colony_.store.oxygen<<L"   H2O "<<colony_.store.water;
        DrawTextLine(x,y,r1.str()); y+=16;

        std::wstringstream r2; r2<<L"Power "<<colony_.powerBalance<<L"   O2 "<<colony_.oxygenBalance<<L"   H2O "<<colony_.waterBalance<<L"   Pop "<<colony_.population<<L"/"<<colony_.housing;
        DrawTextLine(x,y,r2.str()); y+=16;

        std::wstring sel = selected_.has_value()? NameOf(*selected_) : L"None";
        DrawTextLine(x,y, L"Build: "+sel); y+=16;

        SetTextColor(back_.mem, RGB(255,128,64));
        DrawTextLine(x,y, L"1=Solar  2=Hab  3=O2Gen   LMB place  RMB cancel  G colonist  P pause  +/- speed  Arrows pan");

        SelectObject(back_.mem, oldFont);

        if (!banner_.empty() && bannerTime_>0.0) {
            int bw = (int)banner_.size()*8+24; int bh=24;
            RECT b{ (clientW_-bw)/2, clientH_-bh-12, (clientW_+bw)/2, clientH_-12 };
            HBRUSH bb=CreateSolidBrush(RGB(30,30,35)); FillRect(back_.mem,&b,bb); DeleteObject(bb);
            FrameRect(back_.mem, &b, (HBRUSH)GetStockObject(BLACK_BRUSH));
            HFONT of=(HFONT)SelectObject(back_.mem,font_);
            SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(255,255,255));
            RECT trc=b; trc.left+=12; trc.top+=4; DrawTextW(back_.mem, banner_.c_str(), -1, &trc, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(back_.mem,of);
            bannerTime_ -= 0.016;
            if (bannerTime_<=0.0) banner_.clear();
        }
    }

    void DrawTextLine(int x, int y, const std::wstring& s) {
        RECT rc{ x,y,x+1000,y+16 };
        DrawTextW(back_.mem, s.c_str(), -1, &rc, DT_LEFT|DT_TOP|DT_SINGLELINE);
    }

    void DrawTooltip(int x, int y, const std::wstring& text) {
        RECT rc{ x,y,x+(int)text.size()*8+8, y+20 };
        HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(back_.mem,&rc,bg); DeleteObject(bg);
        FrameRect(back_.mem,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HFONT of=(HFONT)SelectObject(back_.mem,font_);
        SetBkMode(back_.mem, TRANSPARENT); SetTextColor(back_.mem, RGB(230,230,240));
        RECT t=rc; t.left+=4; t.top+=2; DrawTextW(back_.mem, text.c_str(), -1, &t, DT_LEFT|DT_TOP|DT_SINGLELINE);
        SelectObject(back_.mem,of);
    }

    RECT TileRect(int tx,int ty) const {
        int px = int((tx*tileSize_ - camera_.x) * zoom_);
        int py = int((ty*tileSize_ - camera_.y) * zoom_);
        int s  = int(tileSize_ * zoom_);
        RECT rc{px,py,px+s,py+s};
        return rc;
    }
    void DrawCell(int x,int y, COLORREF c) {
        RECT rc = TileRect(x,y);
        HBRUSH br=CreateSolidBrush(c); FillRect(back_.mem,&rc,br); DeleteObject(br);
    }

    std::wstring NameOf(BuildingKind k) {
        switch(k){
            case BuildingKind::Solar:  return L"Solar Panel";
            case BuildingKind::Habitat:return L"Habitat";
            case BuildingKind::OxyGen: return L"Oxygen Generator";
        }
        return L"?";
    }
    void Banner(const std::wstring& s) { banner_ = s; bannerTime_ = 3.0; }

private:
    // Win
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    BackBuffer back_;
    HFONT font_ = nullptr;
    int clientW_=1280, clientH_=720;
    UINT dpi_ = 96;

    // Camera
    struct { double x=0,y=0; } camera_;
    double zoom_=1.0;

    // Config/paths
    AppPaths paths_;
    Config   cfg_;

    // World
    World world_;
    Rng   rng_;
    int   tileSize_=24;
    Vec2i hq_{0,0};
    std::vector<Building> buildings_;
    std::optional<Building> pendingBuild_;
    int nextBuildingId_=1;

    std::vector<Colonist> colonists_;
    int nextColonistId_=1;

    Colony colony_;

    // Sim
    bool running_=true, paused_=false;
    double simSpeed_=1.0;
    const double fixedDt_=1.0/60.0;
    double simAcc_=0.0, moveAcc_=0.0, dayTime_=0.25;

    // Input state
    Vec2i keyPan_{0,0};
    bool buildMode_=false; std::optional<BuildingKind> selected_;
    POINT lastMouse_{};

    // Gamepad state
    bool   padConnected_ = false;
    DWORD  padIndex_     = 0;
    XINPUT_STATE padPrev_{};
    double rumbleUntil_  = 0.0;
    double padPanX_      = 0.0;
    double padPanY_      = 0.0;

    // Banner
    std::wstring banner_; double bannerTime_=0.0;
};

//======================================================================================
// Entry point and bootstrap
//======================================================================================
static int ValidateMain(const AppPaths& paths, const LaunchOptions& cli, const Config& eff) {
    std::wstring msg;
    bool ok = ValidateInstallation(&msg);
    // Optional: write settings if given
    if (cli.configFile) {
        WriteDefaultConfig(*cli.configFile, eff);
    } else {
        WriteDefaultConfig(paths.defaultConfig, eff);
    }
    // Return 0 on success like a validator should.
    return ok ? 0 : 4;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Prefer Per‑Monitor v2 DPI awareness (fallback handled internally).
    EnablePerMonitorDpiV2(); // See Microsoft DPI guidance. :contentReference[oaicite:2]{index=2}

    // ARR: register restart and recovery very early, before heavy init.
    InstallWindowsARR();

    AppPaths paths = ComputePaths();

    // Log file
    std::wstring logFile = util::JoinPath(paths.logsDir, L"ColonyGame-" + util::NowStampCompact() + L".log");
    g_log.Open(logFile);
    g_log.Line(L"colonygame.exe starting …");

    // Note if this instance was restarted by WER.
    if (WasRestartedByWer()) {
        g_log.Line(L"[ARR] Restarted after crash/hang; will attempt to load recovery if present.");
    }

    // Parse CLI
    int argc=0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    LaunchOptions cli = ParseArgs(argc, argv);
    if (argv) LocalFree(argv);

    // Load config
    std::wstring cfgPath = cli.configFile.value_or(paths.defaultConfig);
    Config defaults;
    Config fileCfg = LoadConfig(cfgPath, /*create*/ true, defaults);
    Config eff = MakeEffectiveConfig(fileCfg, cli);

    // Seed fallback
    if (!eff.seed.has_value()) {
        std::random_device rd;
        eff.seed = (uint64_t(rd())<<32) ^ uint64_t(rd());
    }

    // Validate mode (no window)
    if (cli.validateOnly) {
        int rc = ValidateMain(paths, cli, eff);
        g_log.Line(util::Format(L"Validate exit code: %d", rc));
        CoUninitialize();
        return rc;
    }

    // Create/Run game
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);

    GameApp app(hInst, paths, eff);
    int rc = app.Run();

    // Clean up XInput loader
    UnloadXInput();

    g_log.Line(util::Format(L"Exit code: %d", rc));
    CoUninitialize();
    return rc;
}
