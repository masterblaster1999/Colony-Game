// ============================================================================
// WinLauncher.cpp — Windows launcher for Colony-Game
// - Parses CLI / sets defaults
// - (Optionally) shows a file-open dialog (--open-save)
// - Calls into the game TU: int RunColonyGame(const GameOptions&)
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <commdlg.h>   // OPENFILENAMEW / GetOpenFileNameW
#include <objbase.h>

#include <cwchar>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>

// Link libs needed by APIs we use here.
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")

// ----------------------------- Game API surface ------------------------------
// Must match src/src-gamesingletu.cpp exactly (layout + names).
struct GameOptions {
    int         width        = 1280;
    int         height       = 720;
    bool        fullscreen   = false;
    bool        vsync        = true;
    bool        safeMode     = false;
    uint64_t    seed         = 0;
    std::string profile      = "default";
    std::string lang         = "en-US";
    std::string saveDir;     // e.g. %LOCALAPPDATA%\ColonyGame\Saves
    std::string assetsDir;   // e.g. .\assets
};
int RunColonyGame(const GameOptions&);  // implemented in the game TU

// --------------------------------- util --------------------------------------
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

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}
static bool EnsureDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS;
}
static std::wstring NowStampCompact() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Robust uint64 parser (decimal or 0x-prefixed hex). Safe default 0 on failure.
static uint64_t ParseU64(const std::wstring& w) {
    try {
        size_t idx = 0;
        unsigned long long v = std::stoull(w, &idx, 0 /* auto base, handles 0x */);
        if (idx == 0) return 0ULL;
        return static_cast<uint64_t>(v);
    } catch (...) { return 0ULL; }
}
static uint64_t ParseU64(const std::string& s) {
    return ParseU64(Widen(s));
}

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}
static std::wstring LocalAppDataSubdir(const std::wstring& sub) {
    PWSTR base = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        out = JoinPath(base, sub);
        CoTaskMemFree(base);
    }
    return out;
}

} // namespace util

// --------------------------------- logging -----------------------------------
class Logger {
public:
    bool Open(const std::wstring& logfile) {
        f_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        return f_.is_open();
    }
    void Line(const std::wstring& s) {
        if (!f_) return;
        std::wstring w = L"[" + util::NowStampCompact() + L"] " + s + L"\r\n";
        // Correct wide write: count is in wchar_t, not bytes
        f_.write(w.c_str(), static_cast<std::streamsize>(w.size()));
        f_.flush();
    }
private:
    std::wofstream f_;
};

static Logger g_log;

// ---------------------------- CLI / dialog helpers ---------------------------
static std::vector<std::wstring> GetArgsW() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv && argc > 0) {
        args.assign(argv, argv + argc);
        LocalFree(argv);
    }
    return args;
}

static bool StartsWith(const std::wstring& s, const std::wstring& pfx) {
    return s.size() >= pfx.size() && std::equal(pfx.begin(), pfx.end(), s.begin());
}
static std::wstring AfterEq(const std::wstring& s) {
    size_t pos = s.find(L'=');
    return (pos == std::wstring::npos) ? L"" : s.substr(pos + 1);
}

static bool PickSaveFile(std::wstring& outPath) {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = nullptr;
    ofn.lpstrFilter = L"Save Files (*.save)\0*.save\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Select Colony Save";
    if (GetOpenFileNameW(&ofn)) {
        outPath = fileBuf;
        return true;
    }
    return false;
}

// --------------------------------- entry -------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Resolve default dirs
    const std::wstring appBase   = util::LocalAppDataSubdir(L"ColonyGame");
    const std::wstring savesDirW = util::JoinPath(appBase, L"Saves");
    const std::wstring logsDirW  = util::JoinPath(appBase, L"Logs");
    const std::wstring assetsDirW= util::JoinPath(util::ExeDir(), L"assets");

    util::EnsureDir(appBase);
    util::EnsureDir(savesDirW);
    util::EnsureDir(logsDirW);

    // Open launcher log
    g_log.Open(util::JoinPath(logsDirW, L"Launcher-" + util::NowStampCompact() + L".log"));
    g_log.Line(L"Launcher start");

    // Defaults
    GameOptions opts;
    opts.saveDir   = util::Narrow(savesDirW);
    opts.assetsDir = util::Narrow(assetsDirW);

    // Parse CLI
    auto args = GetArgsW();
    for (const auto& a : args) {
        if (a == L"--fullscreen") opts.fullscreen = true;
        else if (a == L"--windowed") opts.fullscreen = false;
        else if (a == L"--vsync") opts.vsync = true;
        else if (a == L"--novsync") opts.vsync = false;
        else if (a == L"--safe") opts.safeMode = true;
        else if (a == L"--unsafe") opts.safeMode = false;
        else if (StartsWith(a, L"--width="))  { opts.width  = std::max(320, _wtoi(AfterEq(a).c_str())); }
        else if (StartsWith(a, L"--height=")) { opts.height = std::max(200, _wtoi(AfterEq(a).c_str())); }
        else if (StartsWith(a, L"--seed="))   { opts.seed   = util::ParseU64(AfterEq(a)); }
        else if (StartsWith(a, L"--profile=")){ opts.profile= util::Narrow(AfterEq(a)); }
        else if (StartsWith(a, L"--lang="))   { opts.lang   = util::Narrow(AfterEq(a)); }
        else if (StartsWith(a, L"--save-dir=")){ opts.saveDir = util::Narrow(AfterEq(a)); }
        else if (StartsWith(a, L"--assets-dir=")){ opts.assetsDir = util::Narrow(AfterEq(a)); }
        else if (a == L"--open-save") {
            std::wstring picked;
            if (PickSaveFile(picked)) {
                // If user picked a save file, adopt its directory as saveDir and
                // profile name from filename (basename without extension)
                wchar_t fname[_MAX_FNAME]{}, ext[_MAX_EXT]{}, dir[_MAX_DIR]{}, drive[_MAX_DRIVE]{};
                _wsplitpath_s(picked.c_str(), drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
                std::wstring dirFull = std::wstring(drive) + std::wstring(dir);
                opts.saveDir = util::Narrow(dirFull);
                opts.profile = util::Narrow(std::wstring(fname));
            }
        }
    }

    std::wstringstream ss;
    ss << L"opts: " << (opts.fullscreen?L"fullscreen":L"windowed")
       << L" " << opts.width << L"x" << opts.height
       << L" vsync=" << (opts.vsync?L"on":L"off")
       << L" safeMode=" << (opts.safeMode?L"on":L"off")
       << L" seed=0x" << std::hex << std::uppercase << opts.seed << std::dec
       << L" profile='" << util::Widen(opts.profile) << L"'"
       << L" saveDir='" << util::Widen(opts.saveDir) << L"'"
       << L" assetsDir='" << util::Widen(opts.assetsDir) << L"'";
    g_log.Line(ss.str());

    // Run the game (Win-specific init like COM and common controls is inside)
    int rc = RunColonyGame(opts);

    std::wstringstream done; done << L"Launcher exit rc=" << rc;
    g_log.Line(done.str());
    return rc;
}
