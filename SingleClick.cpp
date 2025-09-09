// SingleClick.cpp
// Windows-only single-file EXE: flexible Launcher + Validator + Embedded Fallback Game.
//
// Key features:
// - Win32 GUI (no resource file)
// - Auto-detects external game EXE (tries common names & subfolders)
// - Writes/reads %APPDATA%\MarsColonySim\settings.ini
// - Builds CLI and launches the game via CreateProcessW
// - Validate button (--validate); Open Saves/Logs/Config shortcuts
// - Embedded fallback mini-game (GDI) if external EXE is missing
//
// Build (MSVC Dev Prompt):
//   cl /EHsc /permissive- /W4 /DUNICODE /DWIN32_LEAN_AND_MEAN SingleClick.cpp ^
//      /link user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib comctl32.lib advapi32.lib ole32.lib
//
// -----------------------------------------------------------------------------
// Notes on compatibility:
// - CLI flags passed: --res WxH --fullscreen --vsync true|false --seed N
//   --safe-mode --skip-intro --lang CODE --profile NAME --config "path\settings.ini"
// - Exit codes: Validate returns 0 on success, non-zero on failure.
// - If no external game executable is found, this EXE can run an embedded
//   mini-game (toggleable) so "single click" still runs.
// -----------------------------------------------------------------------------

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <objbase.h>

#include <cstdint>
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

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Enable v6 Common Controls visual styles without a .manifest
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ------------------------- Helpers & Utilities -------------------------------

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

static std::wstring Format(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    wchar_t buf[4096]; _vsnwprintf_s(buf, 4096, _TRUNCATE, fmt, ap); va_end(ap);
    return buf;
}

static std::wstring GetEnv(const wchar_t* name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768) return L"";
    return std::wstring(buf, n);
}

static std::wstring KnownFolderPath(REFKNOWNFOLDERID fid) {
    PWSTR p = nullptr;
    if (SHGetKnownFolderPath(fid, 0, nullptr, &p) != S_OK) return L"";
    std::wstring s = p; CoTaskMemFree(p);
    return s;
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

template <typename T> static T clamp(T v, T lo, T hi) {
    return std::min(hi, std::max(lo, v));
}

} // namespace util

// ------------------------------- Logging -------------------------------------

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
        f_.write(reinterpret_cast<const char*>(w.c_str()), (std::streamsize)w.size() * sizeof(wchar_t));
        f_.flush();
    }
private:
    std::wofstream f_;
};

static Logger g_log;

// --------------------------- Paths / Configuration ---------------------------

static const wchar_t* kAppName     = L"MarsColonySim";
static const wchar_t* kLauncherWin = L"Mars Colony — Single Click Launcher";
static const wchar_t* kIniName     = L"settings.ini";

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
    p.defaultConfig  = util::JoinPath(p.configDir, kIniName);
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
    return util::Widen(bytes);
}
static bool WriteFileW(const std::wstring& path, const std::wstring& content) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(content.c_str()), (std::streamsize)content.size()*sizeof(wchar_t));
    return true;
}
static void WriteDefaultConfig(const std::wstring& file, const Config& c) {
    std::wstringstream out;
    out << L"# Mars Colony Simulation - settings.ini\r\n"
        << L"# Generated by SingleClick launcher\r\n\r\n"
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

// ------------------------------ Display Modes --------------------------------

struct Mode { UINT w=0, h=0, freq=60; };

static std::vector<Mode> EnumerateDisplayModes() {
    std::vector<Mode> modes;
    DEVMODEW dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
    for (DWORD i=0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel < 24) continue;
        Mode m; m.w = dm.dmPelsWidth; m.h = dm.dmPelsHeight; m.freq = dm.dmDisplayFrequency;
        if (m.w < 800 || m.h < 600) continue;
        modes.push_back(m);
    }
    std::sort(modes.begin(), modes.end(), [](const Mode& a, const Mode& b){
        if (a.w != b.w) return a.w < b.w;
        return a.h < b.h;
    });
    modes.erase(std::unique(modes.begin(), modes.end(), [](const Mode& a, const Mode& b){ return a.w==b.w && a.h==b.h; }), modes.end());
    return modes;
}

// ------------------------------ UI constants ---------------------------------

static const wchar_t* kAppWinClass = L"MCS_SingleClick_Class";
static const wchar_t* kAppTitle    = L"Mars Colony — Single Click";

static const std::array<const wchar_t*, 8> kExeCandidates = {
    L"colonygame.exe",
    L"Colony-Game.exe",
    L"ColonyGame.exe",
    L"MarsColony.exe",
    L"Game.exe",
    L"LauncherTarget.exe",
    L"build\\Release\\colonygame.exe",
    L"bin\\colonygame.exe"
};

enum : int {
    IDC_EXE_EDIT = 1001,
    IDC_EXE_BROWSE,
    IDC_RES_COMBO,
    IDC_FULLSCREEN,
    IDC_VSYNC,
    IDC_SAFE,
    IDC_SKIP,
    IDC_PROFILE_EDIT,
    IDC_LANG_EDIT,
    IDC_SEED_RANDOM,
    IDC_SEED_FIXED,
    IDC_SEED_VALUE,
    IDC_USE_CLI,
    IDC_WRITE_INI,
    IDC_VALIDATE,
    IDC_OPEN_SAVES,
    IDC_OPEN_LOGS,
    IDC_OPEN_CONFIG,
    IDC_PLAY,
    IDC_QUIT,
    IDC_CUSTOM_ARGS,
    IDC_USE_EMBEDDED
};

// ------------------------------ App State ------------------------------------

struct AppState {
    HWND hwnd = nullptr;
    HFONT font = nullptr;

    AppPaths paths;
    Config cfg;

    std::wstring gameExePath;
    std::vector<Mode> modes;

    bool useCli   = true;
    bool writeIni = true;
    bool useEmbeddedIfMissing = true;

    HWND hExeEdit=0, hRes=0, hFull=0, hVsync=0, hSafe=0, hSkip=0,
         hProfile=0, hLang=0, hSeedRandom=0, hSeedFixed=0, hSeedValue=0,
         hUseCli=0, hWriteIni=0, hCustomArgs=0, hUseEmbedded=0;
};

static AppState g;

// ------------------------------ UI helpers -----------------------------------

static HFONT MakeUIFont(int pt=9, bool bold=false) {
    LOGFONTW lf{}; lf.lfHeight = -MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

static void Place(HWND parent, HWND& out, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    out = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x,y,w,h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(out, WM_SETFONT, (WPARAM)g.font, TRUE);
}
static void AddLabel(HWND parent, int x, int y, const wchar_t* t) {
    HWND h; Place(parent, h, L"STATIC", t, SS_LEFT, x, y, 240, 20, 0);
}
static void AddCheckbox(HWND parent, HWND& out, int x, int y, int w, const wchar_t* text, int id) {
    Place(parent, out, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w, 24, id);
}
static void AddButton(HWND parent, HWND& out, int x, int y, int w, const wchar_t* text, int id) {
    Place(parent, out, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x, y, w, 28, id);
}
static void AddEdit(HWND parent, HWND& out, int x, int y, int w, const wchar_t* placeholder, int id) {
    Place(parent, out, L"EDIT", placeholder, WS_TABSTOP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL, x, y, w, 24, id);
}
static void AddCombo(HWND parent, HWND& out, int x, int y, int w, int id) {
    Place(parent, out, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x, y, w, 240, id);
}
static void AddRadio(HWND parent, HWND& out, int x, int y, int w, const wchar_t* text, int id) {
    Place(parent, out, L"BUTTON", text, WS_TABSTOP | BS_AUTORADIOBUTTON, x, y, w, 24, id);
}
static void MsgBox(HWND h, const std::wstring& m, UINT icon=MB_ICONINFORMATION) {
    MessageBoxW(h, m.c_str(), kLauncherWin, MB_OK | icon);
}

// ------------------------------ EXE detection --------------------------------

static std::wstring DetectGameExe() {
    auto dir = util::ExeDir();

    // Try known candidates relative to launcher
    for (auto name : kExeCandidates) {
        auto p = util::JoinPath(dir, name);
        if (util::FileExists(p)) return p;
    }

    // Try common VS/CI output folders
    const std::array<std::wstring, 6> roots = {
        util::JoinPath(dir, L"build\\Release"),
        util::JoinPath(dir, L"build\\RelWithDebInfo"),
        util::JoinPath(dir, L"x64\\Release"),
        util::JoinPath(dir, L"out\\Release"),
        util::JoinPath(dir, L"bin"),
        dir
    };
    for (const auto& r : roots) {
        WIN32_FIND_DATAW fd{};
        auto glob = util::JoinPath(r, L"*.exe");
        HANDLE h = FindFirstFileW(glob.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring n = fd.cFileName;
                    if (_wcsicmp(n.c_str(), L"SingleClick.exe") == 0) continue;
                    // Heuristic: prefer "colony"
                    if (StrStrIW(n.c_str(), L"colony")) {
                        auto p = util::JoinPath(r, n);
                        if (util::FileExists(p)) { FindClose(h); return p; }
                    }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return L"";
}

// ------------------------------ INI & UI sync --------------------------------

static std::vector<Mode> g_modes;

static void PopulateResolutions() {
    g_modes = EnumerateDisplayModes();
    SendMessageW(g.hRes, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i=0;i<g_modes.size();++i) {
        auto s = util::Format(L"%u x %u", g_modes[i].w, g_modes[i].h);
        int idx = (int)SendMessageW(g.hRes, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessageW(g.hRes, CB_SETITEMDATA, idx, (LPARAM)i);
        if (g_modes[i].w == g.cfg.width && g_modes[i].h == g.cfg.height) sel = idx;
    }
    if (sel >= 0) SendMessageW(g.hRes, CB_SETCURSEL, sel, 0);
}

static void LoadIniIntoUI() {
    PopulateResolutions();
    SendMessageW(g.hFull, BM_SETCHECK, g.cfg.fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hVsync,BM_SETCHECK, g.cfg.vsync ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hSafe, BM_SETCHECK, g.cfg.safeMode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hSkip, BM_SETCHECK, g.cfg.skipIntro ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g.hProfile, g.cfg.profile.c_str());
    SetWindowTextW(g.hLang,    g.cfg.lang.c_str());
    if (g.cfg.seed.has_value()) {
        SendMessageW(g.hSeedFixed, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g.hSeedRandom,BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(g.hSeedValue, std::to_wstring(*g.cfg.seed).c_str());
    } else {
        SendMessageW(g.hSeedRandom,BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g.hSeedFixed, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(g.hSeedValue, L"");
    }
    SendMessageW(g.hUseCli,   BM_SETCHECK, g.useCli   ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hWriteIni, BM_SETCHECK, g.writeIni ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hUseEmbedded, BM_SETCHECK, g.useEmbeddedIfMissing ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void ReadUIIntoState() {
    int sel = (int)SendMessageW(g.hRes, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        size_t i = (size_t)SendMessageW(g.hRes, CB_GETITEMDATA, sel, 0);
        if (i < g_modes.size()) { g.cfg.width = g_modes[i].w; g.cfg.height = g_modes[i].h; }
    }
    g.cfg.fullscreen = (SendMessageW(g.hFull, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.vsync      = (SendMessageW(g.hVsync,BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.safeMode   = (SendMessageW(g.hSafe, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.skipIntro  = (SendMessageW(g.hSkip, BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t buf[512];
    GetWindowTextW(g.hProfile, buf, 512); g.cfg.profile = buf;
    GetWindowTextW(g.hLang,    buf, 512); g.cfg.lang    = buf;
    GetWindowTextW(g.hSeedValue, buf, 512);
    if (SendMessageW(g.hSeedFixed, BM_GETCHECK, 0, 0) == BST_CHECKED && buf[0] != 0) {
        g.cfg.seed = ParseU64(buf);
    } else {
        g.cfg.seed.reset();
    }
    g.useCli   = (SendMessageW(g.hUseCli,   BM_GETCHECK, 0,0) == BST_CHECKED);
    g.writeIni = (SendMessageW(g.hWriteIni, BM_GETCHECK, 0,0) == BST_CHECKED);
    g.useEmbeddedIfMissing = (SendMessageW(g.hUseEmbedded, BM_GETCHECK, 0,0) == BST_CHECKED);

    // Reflect exe path edit box
    wchar_t ebuf[MAX_PATH]; GetWindowTextW(g.hExeEdit, ebuf, MAX_PATH); g.gameExePath = ebuf;
}

static void SaveIniIfNeeded() {
    if (!g.writeIni) return;
    util::EnsureDir(g.paths.configDir);
    WriteDefaultConfig(g.paths.defaultConfig, g.cfg);
    g_log.Line(L"Wrote settings.ini -> " + g.paths.defaultConfig);
}

static std::wstring BuildCli() {
    std::wstringstream ss;
    ss << L"--res " << g.cfg.width << L"x" << g.cfg.height << L" ";
    if (g.cfg.fullscreen) ss << L"--fullscreen ";
    ss << L"--vsync " << (g.cfg.vsync ? L"true " : L"false ");
    if (g.cfg.safeMode)  ss << L"--safe-mode ";
    if (g.cfg.skipIntro) ss << L"--skip-intro ";
    if (!g.cfg.profile.empty()) ss << L"--profile " << util::Quoted(g.cfg.profile) << L" ";
    if (!g.cfg.lang.empty())    ss << L"--lang "    << util::Quoted(g.cfg.lang)    << L" ";
    if (g.cfg.seed.has_value()) ss << L"--seed "    << std::to_wstring(*g.cfg.seed) << L" ";
    ss << L"--config " << util::Quoted(g.paths.defaultConfig) << L" ";
    wchar_t extra[1024]{}; GetWindowTextW(g.hCustomArgs, extra, 1024);
    if (extra[0]) { ss << extra << L" "; }
    return ss.str();
}

// ------------------------------ Process helpers ------------------------------

static bool RunChildAndWait(const std::wstring& exe, const std::wstring& args, DWORD& exitCode) {
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    auto cmd = util::Quoted(exe) + (args.empty()? L"" : (L" " + args));
    std::wstring wd = exe.substr(0, exe.find_last_of(L"\\/"));
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi);
    if (!ok) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------ Validator ------------------------------------

static bool ValidateInstallation(std::wstring* messageOut=nullptr) {
    // Rule of thumb: we expect an assets\ folder next to the external game exe if present,
    // otherwise accept that the embedded fallback can run without assets.
    std::wstring exe = g.gameExePath;
    if (exe.empty()) exe = util::JoinPath(util::ExeDir(), L"colonygame.exe");
    std::wstring root = exe.empty()? util::ExeDir() : exe.substr(0, exe.find_last_of(L"\\/"));
    auto assets = util::JoinPath(root, L"assets");
    bool ok = util::DirExists(assets);
    if (!ok) {
        if (messageOut) *messageOut = L"assets\\ not found next to game executable (OK if using embedded fallback).";
        return g.useEmbeddedIfMissing; // if we allow embedded, validation passes
    }
    if (messageOut) *messageOut = L"assets\\ found.";
    return true;
}

// ---------------------------- Embedded Mini-Game ------------------------------
//
// A tiny, dependency-free GDI "game" we can run if the external exe is missing.
// This lets the single EXE still provide a runnable experience on first click.

struct Vec2i { int x=0, y=0; };
struct Tile { int type=0; int r=0; bool walkable=true; uint8_t cost=10; };

class MiniGame {
public:
    MiniGame(UINT w, UINT h, bool fullscreen, bool vsync)
        : reqW_(w), reqH_(h), fullscreen_(fullscreen), vsync_(vsync) {}

    int Run() {
        if (!CreateMainWindow()) return 3;
        GenWorld();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        LARGE_INTEGER freq{}, last{}; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&last);
        double acc=0, dtFixed=1.0/60.0;

        MSG msg{};
        while (running_) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running_=false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            double dt = double(now.QuadPart - last.QuadPart)/double(freq.QuadPart);
            last = now; acc += dt;

            while (acc >= dtFixed) {
                Update(dtFixed);
                acc -= dtFixed;
            }
            Render();
            if (vsync_) Sleep(1);
        }
        return 0;
    }

private:
    static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        MiniGame* self = reinterpret_cast<MiniGame*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(h, m, w, l);
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->WndProc(h, m, w, l);
    }

    bool CreateMainWindow() {
        WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=StaticWndProc;
        wc.hCursor=LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=L"MiniGame_SingleClick";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassW(&wc);

        DWORD style = fullscreen_? WS_POPUP : WS_OVERLAPPEDWINDOW;
        RECT rc{0,0,(LONG)reqW_,(LONG)reqH_};
        AdjustWindowRect(&rc, style, FALSE);
        int W = rc.right-rc.left, H = rc.bottom-rc.top;

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Mars Colony (Embedded)",
            style, CW_USEDEFAULT, CW_USEDEFAULT, W, H, nullptr, nullptr, wc.hInstance, this);
        if (!hwnd_) return false;

        LOGFONTW lf{}; lf.lfHeight = -MulDiv(10, GetDeviceCaps(GetDC(hwnd_), LOGPIXELSY), 72);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = CreateFontIndirectW(&lf);
        return true;
    }

    void GenWorld() {
        W_=80; H_=50; tiles_.assign(W_*H_, {});
        std::mt19937_64 rng(0xC01onyULL);
        for (int y=0;y<H_;++y) for (int x=0;x<W_;++x) {
            Tile& t = tiles_[y*W_+x];
            t.type = (rng()%11==0)?2 : (rng()%7==0?1:0);
            t.walkable = (t.type!=3);
            t.cost = (t.type==1?12 : t.type==2?16 : 10);
            t.r = (t.type==2? rng()%20 : (t.type==1? rng()%8 : 0));
        }
        camX_ = W_*12 - reqW_/2; camY_ = H_*12 - reqH_/2;
    }

    void Update(double /*dt*/) {
        // simple panning with arrow keys
        if (GetAsyncKeyState(VK_LEFT)&0x8000)  camX_ -= 200/60.0;
        if (GetAsyncKeyState(VK_RIGHT)&0x8000) camX_ += 200/60.0;
        if (GetAsyncKeyState(VK_UP)&0x8000)    camY_ -= 200/60.0;
        if (GetAsyncKeyState(VK_DOWN)&0x8000)  camY_ += 200/60.0;
    }

    void Render() {
        HDC hdc=GetDC(hwnd_);
        if (!backMem_ || backW_!=clientW_ || backH_!=clientH_) {
            if (backMem_) { DeleteDC(backMem_); backMem_=0; }
            if (backBmp_) { DeleteObject(backBmp_); backBmp_=0; }
            backW_=clientW_; backH_=clientH_;
            backMem_ = CreateCompatibleDC(hdc);
            backBmp_ = CreateCompatibleBitmap(hdc, backW_, backH_);
            SelectObject(backMem_, backBmp_);
        }

        HBRUSH sky=CreateSolidBrush(RGB(110,50,40));
        RECT full{0,0,clientW_,clientH_}; FillRect(backMem_, &full, sky); DeleteObject(sky);

        int s=16;
        for(int y=0;y<H_;++y) for(int x=0;x<W_;++x) {
            RECT rc = TileRect(x,y,s);
            COLORREF c = (tiles_[y*W_+x].type==0? RGB(150,90,70) : tiles_[y*W_+x].type==1? RGB(100,100,110) : RGB(120,180,200));
            HBRUSH br=CreateSolidBrush(c); FillRect(backMem_, &rc, br); DeleteObject(br);
            FrameRect(backMem_, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }

        // HUD
        HFONT of=(HFONT)SelectObject(backMem_, font_);
        SetBkMode(backMem_, TRANSPARENT);
        SetTextColor(backMem_, RGB(230,230,240));
        RECT hud{8,8,440,112}; HBRUSH bg=CreateSolidBrush(RGB(20,20,26)); FillRect(backMem_,&hud,bg); DeleteObject(bg);
        FrameRect(backMem_,&hud,(HBRUSH)GetStockObject(BLACK_BRUSH));
        RECT tr=hud; tr.left+=8; tr.top+=6;
        DrawTextW(backMem_, L"Embedded fallback running.\n"
                             L"Use the external game exe for full experience.\n"
                             L"Arrows: pan  |  Alt+F4: quit", -1, &tr, DT_LEFT|DT_TOP);
        SelectObject(backMem_, of);

        BitBlt(hdc, 0,0, clientW_,clientH_, backMem_, 0,0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);
    }

    RECT TileRect(int tx,int ty,int s) const {
        int px = int(tx*s - camX_), py = int(ty*s - camY_);
        return RECT{px,py,px+s,py+s};
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch(m){
        case WM_SIZE: clientW_=LOWORD(l); clientH_=HIWORD(l); return 0;
        case WM_DESTROY: running_=false; PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

private:
    HWND hwnd_=0;
    HFONT font_=0;
    HDC backMem_=0; HBITMAP backBmp_=0; int backW_=0, backH_=0;
    int clientW_=1280, clientH_=720;

    int W_=0,H_=0; std::vector<Tile> tiles_;
    double camX_=0, camY_=0;

    UINT reqW_=1280, reqH_=720; bool fullscreen_=false, vsync_=true;
    bool running_=true;
};

// --------------------------- Actions (Validate/Play) --------------------------

static void DoValidate(HWND h) {
    ReadUIIntoState();
    std::wstring msg;
    bool ok = ValidateInstallation(&msg);
    MsgBox(h, ok ? (L"Validation OK.\n" + msg) : (L"Validation failed.\n" + msg), ok?MB_ICONINFORMATION:MB_ICONERROR);
}

static void DoPlay(HWND h) {
    ReadUIIntoState();
    SaveIniIfNeeded();

    // If external exe exists, launch it; else run embedded if allowed.
    bool haveExternal = !g.gameExePath.empty() && util::FileExists(g.gameExePath);

    if (!haveExternal && !g.useEmbeddedIfMissing) {
        MsgBox(h, L"Game executable not found. Please Browse… to select it or enable 'Use embedded fallback'.", MB_ICONWARNING);
        return;
    }

    if (haveExternal) {
        std::wstring args;
        if (g.useCli) args = BuildCli();

        g_log.Line(L"Launching external: " + g.gameExePath + L"  " + args);

        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        auto cmd = util::Quoted(g.gameExePath) + (args.empty()? L"" : (L" " + args));
        std::wstring wd = g.gameExePath.substr(0, g.gameExePath.find_last_of(L"\\/"));
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi);
        if (!ok) {
            MsgBox(h, util::Format(L"Failed to launch the game (error %lu).", GetLastError()), MB_ICONERROR);
            return;
        }
        // Close launcher right away
        PostQuitMessage(0);
        return;
    }

    // Embedded fallback
    g_log.Line(L"No external exe, running embedded fallback.");
    ShowWindow(g.hwnd, SW_MINIMIZE);
    MiniGame mg(g.cfg.width, g.cfg.height, g.cfg.fullscreen, g.cfg.vsync);
    mg.Run();
    ShowWindow(g.hwnd, SW_RESTORE);
}

// ------------------------------- File dialogs --------------------------------

static std::optional<std::wstring> BrowseForExe(HWND parent) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Game Executable";
    if (GetOpenFileNameW(&ofn)) return std::wstring(file);
    return std::nullopt;
}

// ------------------------------- Window Proc ---------------------------------

static void OnCreate(HWND h) {
    g.font = MakeUIFont(9);
    g.paths = ComputePaths();

    auto logFile = util::JoinPath(g.paths.logsDir, L"SingleClick-" + util::NowStampCompact() + L".log");
    g_log.Open(logFile);
    g_log.Line(L"Launcher starting…");

    g.cfg = LoadConfig(g.paths.defaultConfig, /*create*/ true, Config{});
    g.gameExePath = DetectGameExe();

    int x0=16, y=16;

    AddLabel(h, x0, y, L"Game executable:");
    Place(h, g.hExeEdit, L"EDIT", g.gameExePath.c_str(),
        WS_TABSTOP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL, x0+120, y-2, 360, 24, IDC_EXE_EDIT);
    HWND hBrowse; AddButton(h, hBrowse, x0+485, y-3, 80, L"Browse…", IDC_EXE_BROWSE);
    y += 36;

    AddLabel(h, x0, y, L"Resolution:");
    AddCombo(h, g.hRes, x0+120, y-2, 180, IDC_RES_COMBO);
    AddCheckbox(h, g.hFull, x0+320, y-2, 110, L"Fullscreen", IDC_FULLSCREEN);
    AddCheckbox(h, g.hVsync, x0+430, y-2, 100, L"VSync", IDC_VSYNC);
    y += 34;

    AddCheckbox(h, g.hSafe, x0, y, 150, L"Safe mode (software)", IDC_SAFE);
    AddCheckbox(h, g.hSkip, x0+170, y, 200, L"Skip intro", IDC_SKIP);
    y += 34;

    AddLabel(h, x0, y, L"Profile:");
    AddEdit(h, g.hProfile, x0+120, y-2, 160, L"default", IDC_PROFILE_EDIT);
    AddLabel(h, x0+300, y, L"Language:");
    AddEdit(h, g.hLang,    x0+370, y-2, 120, L"en-US", IDC_LANG_EDIT);
    y += 34;

    AddLabel(h, x0, y, L"Seed:");
    AddRadio(h, g.hSeedRandom, x0+120, y-2, 90, L"Random", IDC_SEED_RANDOM);
    AddRadio(h, g.hSeedFixed,  x0+210, y-2, 70, L"Fixed",  IDC_SEED_FIXED);
    AddEdit(h, g.hSeedValue,   x0+290, y-2, 200, L"",      IDC_SEED_VALUE);
    y += 34;

    AddLabel(h, x0, y, L"Custom args:");
    AddEdit(h, g.hCustomArgs, x0+120, y-2, 360, L"", IDC_CUSTOM_ARGS);
    y += 34;

    AddCheckbox(h, g.hUseCli,      x0,       y, 180, L"Pass options via CLI", IDC_USE_CLI);
    AddCheckbox(h, g.hWriteIni,    x0+200,   y, 200, L"Write settings.ini",   IDC_WRITE_INI);
    AddCheckbox(h, g.hUseEmbedded, x0+420,   y, 230, L"Use embedded fallback if EXE missing", IDC_USE_EMBEDDED);
    y += 40;

    HWND hValidate, hPlay, hQuit, hOpenSaves, hOpenLogs, hOpenConfig;
    AddButton(h, hValidate,  x0,       y, 100, L"Validate", IDC_VALIDATE);
    AddButton(h, hOpenSaves, x0+110,   y, 110, L"Open Saves", IDC_OPEN_SAVES);
    AddButton(h, hOpenLogs,  x0+230,   y, 110, L"Open Logs",  IDC_OPEN_LOGS);
    AddButton(h, hOpenConfig,x0+350,   y, 120, L"Open Config",IDC_OPEN_CONFIG);
    AddButton(h, hPlay,      x0+480,   y, 80,  L"Play",       IDC_PLAY);
    AddButton(h, hQuit,      x0+570,   y, 60,  L"Quit",       IDC_QUIT);

    // defaults
    SendMessageW(g.hUseCli,   BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g.hWriteIni, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g.hSeedRandom, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g.hSeedFixed,  BM_SETCHECK, BST_UNCHECKED, 0);

    LoadIniIntoUI();
}

static void OnCommand(HWND h, WPARAM wParam) {
    switch (LOWORD(wParam)) {
    case IDC_EXE_BROWSE: {
        if (auto sel = BrowseForExe(h)) {
            g.gameExePath = *sel;
            SetWindowTextW(g.hExeEdit, g.gameExePath.c_str());
        }
        break; }
    case IDC_VALIDATE: {
        DoValidate(h); break; }
    case IDC_PLAY: {
        DoPlay(h); break; }
    case IDC_QUIT: {
        PostQuitMessage(0); break; }
    case IDC_OPEN_SAVES: {
        util::OpenInExplorer(g.paths.savesDir); break; }
    case IDC_OPEN_LOGS: {
        util::OpenInExplorer(g.paths.logsDir); break; }
    case IDC_OPEN_CONFIG: {
        if (!util::FileExists(g.paths.defaultConfig)) WriteDefaultConfig(g.paths.defaultConfig, g.cfg);
        ShellExecuteW(h, L"open", L"notepad.exe", util::Quoted(g.paths.defaultConfig).c_str(), nullptr, SW_SHOWNORMAL);
        break; }
    case IDC_SEED_RANDOM: {
        EnableWindow(g.hSeedValue, FALSE); SetWindowTextW(g.hSeedValue, L""); break; }
    case IDC_SEED_FIXED: {
        EnableWindow(g.hSeedValue, TRUE); break; }
    default: break;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: OnCreate(h); return 0;
    case WM_COMMAND: OnCommand(h, w); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------- wWinMain ---------------------------------

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    g.paths = ComputePaths();
    g.cfg   = LoadConfig(g.paths.defaultConfig, /*create*/ true, Config{});

    WNDCLASSW wc{}; wc.hInstance = hInst; wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon   = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = kAppWinClass;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kAppWinClass, kAppTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 380, nullptr, nullptr, hInst, nullptr);

    g.hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g.font) DeleteObject(g.font);
    CoUninitialize();
    return 0;
}
