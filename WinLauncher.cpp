// WinLauncher.cpp
// Windows-only flexible launcher for the Mars Colony Simulation / Colony-Game.
//
// - No dependencies beyond Win32 / Common Controls
// - GUI window with resolution, fullscreen, vsync, seed, profile, language
// - Writes settings.ini (fallback) AND/OR passes CLI to game EXE
// - Validate (runs --validate), Open Saves/Logs/Config, logging, DPI-aware
//
// Build (MSVC Dev Prompt):
//   cl /EHsc /permissive- /W4 /DUNICODE /DWIN32_LEAN_AND_MEAN WinLauncher.cpp ^
//      /link user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib comctl32.lib advapi32.lib ole32.lib
//
// Notes:
// - Default app id/name "MarsColonySim" to match earlier code paths.
// - Adjust kExeCandidates if your game exe has a different name.
//
// -----------------------------------------------------------------------------

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
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

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

// Enable v6 Common Controls visual styles without a .manifest file
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// --------------------------------- Helpers -----------------------------------

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

static void MsgBox(HWND h, const std::wstring& t, const std::wstring& m, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(h, m.c_str(), t.c_str(), MB_OK | icon);
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

static std::wstring GetExeDir() {
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

} // namespace util

// --------------------------------- Logging -----------------------------------

class Logger {
public:
    bool Open(const std::wstring& logfile) {
        file_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        return file_.is_open();
    }
    void Line(const std::wstring& s) {
        if (!file_) return;
        auto t = util::NowStampCompact();
        std::wstring w = L"[" + t + L"] " + s + L"\r\n";
        file_.write(reinterpret_cast<const char*>(w.c_str()), (std::streamsize)w.size() * sizeof(wchar_t));
        file_.flush();
    }
private:
    std::wofstream file_;
};

static Logger g_log;

// ------------------------- App Paths & INI Handling --------------------------

struct AppPaths {
    std::wstring configDir;     // %APPDATA%\MarsColonySim
    std::wstring dataDir;       // %LOCALAPPDATA%\MarsColonySim
    std::wstring savesDir;      // dataDir\Saves
    std::wstring logsDir;       // dataDir\Logs
    std::wstring modsDir;       // dataDir\Mods
    std::wstring screenshotsDir;// dataDir\Screenshots
    std::wstring defaultConfig; // configDir\settings.ini
};

static AppPaths ComputePaths(const std::wstring& appName) {
    AppPaths p;
    std::wstring appdata  = util::GetEnv(L"APPDATA");
    std::wstring localapp = util::GetEnv(L"LOCALAPPDATA");
    if (appdata.empty()) {
        appdata = util::KnownFolderPath(FOLDERID_RoamingAppData);
    }
    if (localapp.empty()) {
        localapp = util::KnownFolderPath(FOLDERID_LocalAppData);
    }
    p.configDir      = util::JoinPath(appdata, appName);
    p.dataDir        = util::JoinPath(localapp, appName);
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

// Tiny .ini reader/writer (INIs are very small)
static std::wstring ReadTextFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return L"";
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return util::Widen(bytes);
}
static bool WriteTextFile(const std::wstring& path, const std::wstring& content) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(content.c_str()), (std::streamsize)content.size()*sizeof(wchar_t));
    return true;
}

static void WriteDefaultConfig(const std::wstring& file, const Config& c) {
    std::wstringstream out;
    out << L"# Mars Colony Simulation - settings.ini\r\n"
        << L"# Windows launcher generated\r\n\r\n"
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
    WriteTextFile(file, out.str());
}

static bool ParseBool(const std::wstring& s, bool fallback=false) {
    auto t = s; std::transform(t.begin(), t.end(), t.begin(), ::towlower);
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
    std::wistringstream in(ReadTextFile(file));
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::wstring t = line;
        // strip comments
        auto posc = t.find_first_of(L"#;/");
        if (posc != std::wstring::npos && (t[posc] == L'#' || t[posc]==L';' || (t[posc]==L'/' && posc+1<t.size() && t[posc+1]==L'/')))
            t = t.substr(0, posc);
        auto pos = t.find(L'=');
        if (pos == std::wstring::npos) continue;
        std::wstring key = t.substr(0,pos); std::wstring val = t.substr(pos+1);
        // trim
        key.erase(0, key.find_first_not_of(L" \t\r\n")); key.erase(key.find_last_not_of(L" \t\r\n")+1);
        val.erase(0, val.find_first_not_of(L" \t\r\n")); val.erase(val.find_last_not_of(L" \t\r\n")+1);
        // map
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

// ----------------------------- Display enumeration ---------------------------

struct Mode { UINT w=0, h=0, freq=60; };

static std::vector<Mode> EnumerateDisplayModes() {
    std::vector<Mode> modes;
    DEVMODEW dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
    for (DWORD i=0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel < 24) continue; // ignore low color depth modes
        Mode m; m.w = dm.dmPelsWidth; m.h = dm.dmPelsHeight; m.freq = dm.dmDisplayFrequency;
        if (m.w < 800 || m.h < 600) continue; // ignore tiny modes
        modes.push_back(m);
    }
    // unique by WxH
    std::sort(modes.begin(), modes.end(), [](const Mode& a, const Mode& b){
        if (a.w != b.w) return a.w < b.w;
        return a.h < b.h;
    });
    modes.erase(std::unique(modes.begin(), modes.end(), [](const Mode& a, const Mode& b){ return a.w==b.w && a.h==b.h; }), modes.end());
    return modes;
}

// ------------------------------- UI constants --------------------------------

static const wchar_t* kAppWinClass = L"MCS_WinLauncher_Class";
static const wchar_t* kAppTitle    = L"Mars Colony — Windows Launcher";

static const wchar_t* kAppName     = L"MarsColonySim"; // folder names under AppData
static const wchar_t* kIniName     = L"settings.ini";

static const std::array<const wchar_t*, 5> kExeCandidates = {
    L"MarsColonyLauncher.exe", L"Colony-Game.exe", L"ColonyGame.exe", L"Game.exe", L"Launcher.exe"
};

// Controls IDs
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
    IDC_CUSTOM_ARGS
};

// ------------------------------ App State ------------------------------------

struct AppState {
    HWND hwnd = nullptr;
    HFONT font = nullptr;

    AppPaths paths;
    Config cfg;

    std::wstring gameExePath;       // resolved exe path
    std::vector<Mode> modes;

    bool useCli   = true;
    bool writeIni = true;

    // UI handles
    HWND hExeEdit=0, hRes=0, hFull=0, hVsync=0, hSafe=0, hSkip=0,
         hProfile=0, hLang=0, hSeedRandom=0, hSeedFixed=0, hSeedValue=0,
         hUseCli=0, hWriteIni=0, hCustomArgs=0;
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

// ------------------------------ Launcher logic --------------------------------

static std::wstring DetectGameExe() {
    auto dir = util::GetExeDir();
    for (auto name : kExeCandidates) {
        auto p = util::JoinPath(dir, name);
        if (util::FileExists(p)) return p;
    }
    // Find any .exe != this
    WIN32_FIND_DATAW fd{};
    auto glob = util::JoinPath(dir, L"*.exe");
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring n = fd.cFileName;
                if (_wcsicmp(n.c_str(), L"WinLauncher.exe") == 0) continue;
                auto p = util::JoinPath(dir, n);
                if (util::FileExists(p)) { FindClose(h); return p; }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return L"";
}

static void PopulateResolutions() {
    g.modes = EnumerateDisplayModes();
    SendMessageW(g.hRes, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i=0;i<g.modes.size();++i) {
        auto s = util::Format(L"%u x %u", g.modes[i].w, g.modes[i].h);
        int idx = (int)SendMessageW(g.hRes, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessageW(g.hRes, CB_SETITEMDATA, idx, (LPARAM)i);
        if (g.modes[i].w == g.cfg.width && g.modes[i].h == g.cfg.height) sel = idx;
    }
    if (sel >= 0) SendMessageW(g.hRes, CB_SETCURSEL, sel, 0);
}

static void LoadIniIntoUI() {
    // Resolution
    PopulateResolutions();
    // Checkboxes
    SendMessageW(g.hFull, BM_SETCHECK, g.cfg.fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hVsync, BM_SETCHECK, g.cfg.vsync ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hSafe,  BM_SETCHECK, g.cfg.safeMode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hSkip,  BM_SETCHECK, g.cfg.skipIntro ? BST_CHECKED : BST_UNCHECKED, 0);
    // Profile/Lang
    SetWindowTextW(g.hProfile, g.cfg.profile.c_str());
    SetWindowTextW(g.hLang,    g.cfg.lang.c_str());
    // Seed
    if (g.cfg.seed.has_value()) {
        SendMessageW(g.hSeedFixed, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g.hSeedRandom,BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(g.hSeedValue, std::to_wstring(*g.cfg.seed).c_str());
    } else {
        SendMessageW(g.hSeedRandom,BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g.hSeedFixed, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(g.hSeedValue, L"");
    }
}

static void ReadUIIntoIni() {
    // Resolution
    int sel = (int)SendMessageW(g.hRes, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        size_t i = (size_t)SendMessageW(g.hRes, CB_GETITEMDATA, sel, 0);
        if (i < g.modes.size()) { g.cfg.width = g.modes[i].w; g.cfg.height = g.modes[i].h; }
    }
    // Toggles
    g.cfg.fullscreen = (SendMessageW(g.hFull, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.vsync      = (SendMessageW(g.hVsync,BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.safeMode   = (SendMessageW(g.hSafe, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.cfg.skipIntro  = (SendMessageW(g.hSkip, BM_GETCHECK, 0, 0) == BST_CHECKED);
    // Text
    wchar_t buf[512];
    GetWindowTextW(g.hProfile, buf, 512); g.cfg.profile = buf;
    GetWindowTextW(g.hLang,    buf, 512); g.cfg.lang    = buf;
    GetWindowTextW(g.hSeedValue, buf, 512);
    if (SendMessageW(g.hSeedFixed, BM_GETCHECK, 0, 0) == BST_CHECKED && buf[0] != 0) {
        g.cfg.seed = util::ParseU64(buf);
    } else {
        g.cfg.seed.reset();
    }
    // Flags
    g.useCli   = (SendMessageW(g.hUseCli,   BM_GETCHECK, 0,0) == BST_CHECKED);
    g.writeIni = (SendMessageW(g.hWriteIni, BM_GETCHECK, 0,0) == BST_CHECKED);
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
    // Config path (explicit), so game definitely knows where to read
    ss << L"--config " << util::Quoted(g.paths.defaultConfig) << L" ";

    // Append any custom args
    wchar_t extra[1024]{}; GetWindowTextW(g.hCustomArgs, extra, 1024);
    if (extra[0]) { ss << extra << L" "; }
    return ss.str();
}

static bool RunChildAndWait(const std::wstring& exe, const std::wstring& args, DWORD& exitCode) {
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    auto cmd = util::Quoted(exe) + L" " + args;
    std::wstring wd = exe.substr(0, exe.find_last_of(L"\\/"));
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi);
    if (!ok) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static void DoValidate(HWND h) {
    if (!util::FileExists(g.gameExePath)) {
        util::MsgBox(h, kAppTitle, L"Game executable not found. Please browse to it.", MB_ICONWARNING);
        return;
    }
    std::wstring args = L"--validate ";
    args += L"--config "; args += util::Quoted(g.paths.defaultConfig);
    DWORD ec=0;
    g_log.Line(L"Validate: " + g.gameExePath + L"  " + args);
    if (!RunChildAndWait(g.gameExePath, args, ec)) {
        util::MsgBox(h, kAppTitle, L"Failed to run the game for validation.", MB_ICONERROR);
        return;
    }
    if (ec == 0) util::MsgBox(h, kAppTitle, L"Validation OK.");
    else         util::MsgBox(h, kAppTitle, util::Format(L"Validation failed (exit code %lu).", ec), MB_ICONERROR);
}

static void DoPlay(HWND h) {
    if (!util::FileExists(g.gameExePath)) {
        util::MsgBox(h, kAppTitle, L"Game executable not found. Please browse to it.", MB_ICONWARNING);
        return;
    }
    ReadUIIntoIni();
    SaveIniIfNeeded();

    std::wstring args;
    if (g.useCli) args = BuildCli();

    g_log.Line(L"Launching: " + g.gameExePath + L"  " + args);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    auto cmd = util::Quoted(g.gameExePath) + (args.empty()? L"" : (L" " + args));
    std::wstring wd = g.gameExePath.substr(0, g.gameExePath.find_last_of(L"\\/"));
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi);
    if (!ok) {
        util::MsgBox(h, kAppTitle, util::Format(L"Failed to launch the game (error %lu).", GetLastError()), MB_ICONERROR);
        return;
    }
    // Option: close launcher right away
    PostQuitMessage(0);
}

// ------------------------------ File dialogs ---------------------------------

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

// ------------------------------ Window Procedure -----------------------------

static void OnCreate(HWND h) {
    g.font = MakeUIFont(9);

    // Prepare logging
    g.paths = ComputePaths(kAppName);
    auto logFile = util::JoinPath(g.paths.logsDir, L"WinLauncher-" + util::NowStampCompact() + L".log");
    g_log.Open(logFile);
    g_log.Line(L"Launcher starting…");

    // Defaults & config
    g.cfg = LoadConfig(g.paths.defaultConfig, /*create*/ true, Config{});
    g.gameExePath = DetectGameExe();

    // Layout
    int x0=16, y=16, wcol=640, xR=x0+420, xRR=x0+520;

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

    AddCheckbox(h, g.hUseCli,   x0,       y, 180, L"Pass options via CLI", IDC_USE_CLI);
    AddCheckbox(h, g.hWriteIni, x0+200,   y, 200, L"Write settings.ini",   IDC_WRITE_INI);
    y += 40;

    HWND hValidate, hPlay, hQuit, hOpenSaves, hOpenLogs, hOpenConfig;
    AddButton(h, hValidate,  x0,       y, 100, L"Validate", IDC_VALIDATE);
    AddButton(h, hOpenSaves, x0+110,   y, 110, L"Open Saves", IDC_OPEN_SAVES);
    AddButton(h, hOpenLogs,  x0+230,   y, 110, L"Open Logs",  IDC_OPEN_LOGS);
    AddButton(h, hOpenConfig,x0+350,   y, 120, L"Open Config",IDC_OPEN_CONFIG);
    AddButton(h, hPlay,      x0+480,   y, 80,  L"Play",       IDC_PLAY);
    AddButton(h, hQuit,      x0+570,   y, 60,  L"Quit",       IDC_QUIT);

    // State defaults
    SendMessageW(g.hUseCli,   BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g.hWriteIni, BM_SETCHECK, BST_CHECKED, 0);

    LoadIniIntoUI();
}

static void OnCommand(HWND h, WPARAM wParam, LPARAM) {
    switch (LOWORD(wParam)) {
    case IDC_EXE_BROWSE: {
        if (auto sel = BrowseForExe(h)) {
            g.gameExePath = *sel;
            SetWindowTextW(g.hExeEdit, g.gameExePath.c_str());
        }
        break; }
    case IDC_VALIDATE: {
        ReadUIIntoIni();
        // Reflect exe edit box
        wchar_t buf[MAX_PATH]; GetWindowTextW(g.hExeEdit, buf, MAX_PATH); g.gameExePath = buf;
        DoValidate(h); break; }
    case IDC_PLAY: {
        ReadUIIntoIni();
        wchar_t buf[MAX_PATH]; GetWindowTextW(g.hExeEdit, buf, MAX_PATH); g.gameExePath = buf;
        DoPlay(h); break; }
    case IDC_QUIT: {
        PostQuitMessage(0); break; }
    case IDC_OPEN_SAVES: {
        util::OpenInExplorer(g.paths.savesDir); break; }
    case IDC_OPEN_LOGS: {
        util::OpenInExplorer(g.paths.logsDir); break; }
    case IDC_OPEN_CONFIG: {
        // Create default config if missing, then open in Notepad
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
    case WM_COMMAND: OnCommand(h, w, l); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------- wWinMain ---------------------------------

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // DPI-aware (System aware is enough for a simple tool)
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{}; wc.hInstance = hInst; wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon   = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = kAppWinClass;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kAppWinClass, kAppTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 360, nullptr, nullptr, hInst, nullptr);

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
