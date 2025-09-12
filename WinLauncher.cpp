// ============================================================================
// WinLauncher.cpp — Windows launcher for Colony-Game
// - Applies patch 4): remove unused narrow ParseU64 overload (C4505) and
//   centralize Windows header policy via platform/win_base.hpp (if present).
// - Massively enhanced robustness and UX for Windows:
//     * Single-instance guard (override with --multiinstance)
//     * DPI awareness (Per-Monitor v2 if available; fallback to system DPI aware)
//     * Safer CLI parsing: supports --key=value and --key value forms
//     * Environment-variable expansion for directories
//     * IFileOpenDialog save picker with fallback to GetOpenFileNameW
//     * COM RAII init, error-mode tweaks, set current directory to exe dir
//     * Improved logging (file + OutputDebugString)
//     * Sanity checks and clamping for resolution
// - Calls into the game TU: int RunColonyGame(const GameOptions&)
// ============================================================================

#if !defined(_WIN32)
#  error "This file is Windows-only."
#endif

// Prefer centralized platform header if available.
#if __has_include("platform/win_base.hpp")
  #include "platform/win_base.hpp"
  #include <Windows.h>
#else
  #ifndef UNICODE
  #  define UNICODE
  #endif
  #ifndef _UNICODE
  #  define _UNICODE
  #endif
  #ifndef NOMINMAX
  #  define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #  define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
#endif

#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <commdlg.h>    // OPENFILENAMEW / GetOpenFileNameW
#include <objbase.h>

#include <cwchar>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <utility>

// Link libs needed by APIs we use here.
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")

#ifndef CG_UNUSED
#  define CG_UNUSED(x) (void)(x)
#endif

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

// -------------------------------- utilities ----------------------------------
namespace util {

static inline void debugf(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

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
    const wchar_t c = a.back();
    if (c == L'\\' || c == L'/') return a + b;
    return a + L"\\" + b;
}
static bool EnsureDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS;
}
static bool FileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring ExpandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    wchar_t tmp[4096];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), tmp, _countof(tmp));
    return (n > 0 && n < _countof(tmp)) ? std::wstring(tmp, n - 1) : s;
}
static std::wstring NowStampCompact() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}
static std::wstring ExePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
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

// Robust uint64 parser (decimal or 0x-prefixed hex). Safe default 0 on failure.
static uint64_t ParseU64(const std::wstring& w) {
    try {
        size_t idx = 0;
        unsigned long long v = std::stoull(w, &idx, 0 /* auto base, handles 0x */);
        if (idx == 0) return 0ULL;
        return static_cast<uint64_t>(v);
    } catch (...) { return 0ULL; }
}

// Basic clamp helpers
template <typename T>
static T clamp(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

} // namespace util

// --------------------------------- logging -----------------------------------
class Logger {
public:
    bool Open(const std::wstring& logfile) {
        f_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        opened_ = f_.is_open();
        return opened_;
    }
    void Line(const std::wstring& s) {
        const std::wstring msg = L"[" + util::NowStampCompact() + L"] " + s + L"\r\n";
        if (opened_) {
            f_.write(msg.c_str(), static_cast<std::streamsize>(msg.size()));
            f_.flush();
        }
        // Also mirror to debugger
        OutputDebugStringW(msg.c_str());
    }
private:
    std::wofstream f_;
    bool opened_ = false;
};

static Logger g_log;

// ------------------------------ RAII helpers ---------------------------------
class ComInit {
public:
    ComInit() {
        hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ComInit() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    HRESULT hr() const { return hr_; }
private:
    HRESULT hr_ = E_FAIL;
};

class SingleInstanceGuard {
public:
    SingleInstanceGuard(const wchar_t* name, bool allowMultiple) {
        if (allowMultiple) return;
        mutex_ = CreateMutexW(nullptr, FALSE, name);
        if (mutex_) {
            const DWORD gle = GetLastError();
            already_ = (gle == ERROR_ALREADY_EXISTS);
        }
    }
    ~SingleInstanceGuard() {
        if (mutex_) CloseHandle(mutex_);
    }
    bool alreadyRunning() const { return already_; }
private:
    HANDLE mutex_ = nullptr;
    bool   already_ = false;
};

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

// Helper: read value in either "--key=value" or "--key value" forms.
// Advances index i if it consumes the next token.
static std::wstring ReadArgValue(const std::vector<std::wstring>& args, size_t& i) {
    const std::wstring& a = args[i];
    const size_t eq = a.find(L'=');
    if (eq != std::wstring::npos) return a.substr(eq + 1);
    if (i + 1 < args.size()) {
        const std::wstring& nxt = args[i + 1];
        if (!nxt.empty() && nxt[0] != L'-') { ++i; return nxt; }
    }
    return L"";
}

static std::wstring MakeUsage() {
    std::wstringstream u;
    u << L"Colony-Game Launcher options:\n"
      << L"  --fullscreen | --windowed\n"
      << L"  --vsync | --novsync\n"
      << L"  --safe | --unsafe\n"
      << L"  --width=<px>  | --width <px>  (min 320)\n"
      << L"  --height=<px> | --height <px> (min 200)\n"
      << L"  --seed=<u64>  (decimal or 0xHEX)\n"
      << L"  --profile=<name>\n"
      << L"  --lang=<tag>            (e.g., en-US)\n"
      << L"  --save-dir=<path>       (env vars allowed, e.g., %USERPROFILE%)\n"
      << L"  --assets-dir=<path>     (env vars allowed)\n"
      << L"  --open-save             (pick a .save file; sets saveDir & profile)\n"
      << L"  --multiinstance         (allow multiple launcher instances)\n"
      << L"  --help | -h | /?\n";
    return u.str();
}

// Modern file picker (IFileOpenDialog). Falls back to GetOpenFileNameW.
static bool PickSaveFile(std::wstring& outPath) {
    // Try IFileOpenDialog first (requires COM).
    HRESULT hr;
    IFileOpenDialog* pDlg = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
    if (SUCCEEDED(hr) && pDlg) {
        // Filter: *.save
        COMDLG_FILTERSPEC filters[] = {
            { L"Save Files (*.save)", L"*.save" },
            { L"All Files (*.*)",     L"*.*" }
        };
        pDlg->SetFileTypes(_countof(filters), filters);
        pDlg->SetFileTypeIndex(1);
        pDlg->SetTitle(L"Select Colony Save");
        pDlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

        hr = pDlg->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    outPath = psz;
                    CoTaskMemFree(psz);
                    pItem->Release();
                    pDlg->Release();
                    return true;
                }
                if (pItem) pItem->Release();
            }
        }
        pDlg->Release();
    }

    // Fallback: old common dialog.
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

// Apply modern DPI awareness when available.
static void ApplyDPIAwareness() {
    // Try Per-Monitor v2
    using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto setCtx = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
            GetProcAddress(hUser, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            // -4 is DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 cast-to-HANDLE; use documented macro if available.
            const HANDLE PER_MON_V2 = (HANDLE)(-4);
            if (setCtx(PER_MON_V2)) return;
        }
        // Fallback: SetProcessDPIAware (system DPI)
        using SetProcessDPIAware_t = BOOL (WINAPI*)();
        auto setAware = reinterpret_cast<SetProcessDPIAware_t>(
            GetProcAddress(hUser, "SetProcessDPIAware"));
        if (setAware) setAware();
    }
}

// ------------------------------- entry point ---------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Reduce intrusive Windows error popups for file/GPU driver issues.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // DPI awareness helps WinForms/ImGui/UWP-hosted windows render crisply.
    ApplyDPIAwareness();

    // Initialize COM for shell APIs (KnownFolder, dialogs, etc.)
    ComInit com;
    CG_UNUSED(com);

    // Prefer running relative to the executable directory.
    SetCurrentDirectoryW(util::ExeDir().c_str());

    // Resolve default dirs (allow environment override)
    const std::wstring appBaseEnv = util::ExpandEnv(L"%LOCALAPPDATA%\\ColonyGame");
    const std::wstring appBase    = util::DirExists(util::LocalAppDataSubdir(L"ColonyGame"))
                                  ? util::LocalAppDataSubdir(L"ColonyGame")
                                  : appBaseEnv;

    const std::wstring savesDirW  = util::JoinPath(appBase, L"Saves");
    const std::wstring logsDirW   = util::JoinPath(appBase, L"Logs");
    const std::wstring assetsDirW = util::JoinPath(util::ExeDir(), L"assets");

    util::EnsureDir(appBase);
    util::EnsureDir(savesDirW);
    util::EnsureDir(logsDirW);

    // Open launcher log (mirror to debugger too)
    g_log.Open(util::JoinPath(logsDirW, L"Launcher-" + util::NowStampCompact() + L".log"));
    g_log.Line(L"Launcher start — exe=" + util::ExePath());

    // Parse CLI
    auto args = GetArgsW();

    // Single-instance guard (unless overridden by --multiinstance)
    bool allowMulti = false;
    for (const auto& a : args) if (a == L"--multiinstance") { allowMulti = true; break; }
    SingleInstanceGuard instanceGuard(L"ColonyGame_Launcher_Singleton", allowMulti);
    if (instanceGuard.alreadyRunning()) {
        g_log.Line(L"Another instance detected; exiting. Use --multiinstance to override.");
        MessageBoxW(nullptr,
                    L"Colony-Game is already running.\n\n"
                    L"Use --multiinstance if you really want to start another instance.",
                    L"Colony-Game", MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    GameOptions opts;
    opts.saveDir   = util::Narrow(savesDirW);
    opts.assetsDir = util::Narrow(assetsDirW);

    // CLI parsing (supports both --key=value and --key value)
    bool showHelp = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];

        if (a == L"--fullscreen") opts.fullscreen = true;
        else if (a == L"--windowed") opts.fullscreen = false;
        else if (a == L"--vsync") opts.vsync = true;
        else if (a == L"--novsync") opts.vsync = false;
        else if (a == L"--safe") opts.safeMode = true;
        else if (a == L"--unsafe") opts.safeMode = false;
        else if (a == L"--help" || a == L"-h" || a == L"/?") showHelp = true;
        else if (StartsWith(a, L"--width")) {
            std::wstring v = ReadArgValue(args, i);
            if (!v.empty()) opts.width = util::clamp(_wtoi(v.c_str()), 320, 16384);
        }
        else if (StartsWith(a, L"--height")) {
            std::wstring v = ReadArgValue(args, i);
            if (!v.empty()) opts.height = util::clamp(_wtoi(v.c_str()), 200, 16384);
        }
        else if (StartsWith(a, L"--seed")) {
            std::wstring v = ReadArgValue(args, i);
            if (!v.empty()) opts.seed = util::ParseU64(v);
        }
        else if (StartsWith(a, L"--profile")) {
            std::wstring v = ReadArgValue(args, i);
            if (!v.empty()) opts.profile = util::Narrow(v);
        }
        else if (StartsWith(a, L"--lang")) {
            std::wstring v = ReadArgValue(args, i);
            if (!v.empty()) opts.lang = util::Narrow(v);
        }
        else if (StartsWith(a, L"--save-dir")) {
            std::wstring v = util::ExpandEnv(ReadArgValue(args, i));
            if (!v.empty()) opts.saveDir = util::Narrow(v);
        }
        else if (StartsWith(a, L"--assets-dir")) {
            std::wstring v = util::ExpandEnv(ReadArgValue(args, i));
            if (!v.empty()) opts.assetsDir = util::Narrow(v);
        }
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
        // Unknown args are ignored to keep the launcher resilient.
    }

    if (showHelp) {
        const std::wstring usage = MakeUsage();
        g_log.Line(L"Showing help.");
        MessageBoxW(nullptr, usage.c_str(), L"Colony-Game Launcher", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Clamp resolution sensibly and correct accidental zero/negatives.
    opts.width  = util::clamp(opts.width,  320, 16384);
    opts.height = util::clamp(opts.height, 200, 16384);

    // Validate assets directory presence; warn (but let the game decide) if not found.
    const std::wstring assetsW = util::ExpandEnv(util::Widen(opts.assetsDir));
    if (!util::DirExists(assetsW)) {
        std::wstring warn = L"Assets directory not found: " + assetsW + L"\n"
                            L"The game may fail to start.";
        g_log.Line(warn);
        // Soft warning only; do not block.
    }

    // Ensure save directory exists (create if needed).
    const std::wstring saveW = util::ExpandEnv(util::Widen(opts.saveDir));
    if (!util::DirExists(saveW)) {
        if (!util::EnsureDir(saveW)) {
            g_log.Line(L"Failed to create saveDir: " + saveW);
        }
    }

    // Log effective options
    std::wstringstream ss;
    ss << L"opts: " << (opts.fullscreen ? L"fullscreen" : L"windowed")
       << L" " << opts.width << L"x" << opts.height
       << L" vsync=" << (opts.vsync ? L"on" : L"off")
       << L" safeMode=" << (opts.safeMode ? L"on" : L"off")
       << L" seed=0x" << std::hex << std::uppercase << opts.seed << std::dec
       << L" profile='" << util::Widen(opts.profile) << L"'"
       << L" saveDir='" << util::Widen(opts.saveDir) << L"'"
       << L" assetsDir='" << util::Widen(opts.assetsDir) << L"'";
    g_log.Line(ss.str());

    // Run the game
    int rc = RunColonyGame(opts);

    std::wstringstream done; done << L"Launcher exit rc=" << rc;
    g_log.Line(done.str());
    return rc;
}
