#include "WinPlatformPaths.h"

#pragma comment(lib, "Shell32.lib") // SHGetKnownFolderPath
#pragma comment(lib, "Ole32.lib")   // CoTaskMemFree

#include <cassert>
#include <system_error>

namespace fs = std::filesystem;

namespace winpath {

static AppPaths g_paths;
static bool     g_inited = false;

// ---------------- UTF conversion ----------------

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                        static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

// ---------------- Paths core ----------------

static fs::path GetModuleFileNameW_Robust() {
    // Microsoft notes that if the buffer is too small, the string is truncated
    // and the function returns the buffer size; we loop and grow. :contentReference[oaicite:4]{index=4}
    std::wstring buf;
    buf.resize(512);
    for (;;) {
        DWORD copied = ::GetModuleFileNameW(nullptr, buf.data(),
                                            static_cast<DWORD>(buf.size()));
        if (copied == 0) {
            // Hard failure
            return {};
        }
        if (copied < buf.size() - 1) {
            buf.resize(copied);
            break;
        }
        // Grow and retry.
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf);
}

fs::path GetExecutablePath() {
    return GetModuleFileNameW_Robust();
}

fs::path GetExecutableDir() {
    auto p = GetExecutablePath();
    return p.empty() ? fs::path{} : p.parent_path();
}

fs::path GetKnownFolder(REFKNOWNFOLDERID id) {
    // SHGetKnownFolderPath allocates with the COM task allocator; you must free
    // with CoTaskMemFree. Returns a path without trailing backslash. :contentReference[oaicite:5]{index=5}
    PWSTR p = nullptr;
    HRESULT hr = ::SHGetKnownFolderPath(id, 0, nullptr, &p);
    if (FAILED(hr)) {
        throw std::system_error(hr, std::system_category(),
                                "SHGetKnownFolderPath failed");
    }
    fs::path out = p;
    ::CoTaskMemFree(p); // documented requirement :contentReference[oaicite:6]{index=6}
    return out;
}

fs::path GetTempFolder() {
    // GetTempPathW consults TMP/TEMP/USERPROFILE/Windows (in that order). :contentReference[oaicite:7]{index=7}
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetTempPathW(static_cast<DWORD>(std::size(buf)), buf);
    if (n == 0 || n >= std::size(buf)) return {};
    return fs::path(buf);
}

static void EnsureWorkingDirectoryIsExeDir(const fs::path& exeDir) {
    // SetCurrentDirectory affects the whole process; call it once at startup,
    // before worker threads begin, to make relative asset paths consistent. :contentReference[oaicite:8]{index=8}
    if (!exeDir.empty()) {
        ::SetCurrentDirectoryW(exeDir.c_str());
    }
}

static void HardenDllSearchPath(const fs::path& exeDir) {
    // Prefer the modern model:
    //   SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
    //   AddDllDirectory(exeDir)
    // and rely on the documented search order. :contentReference[oaicite:9]{index=9}
    using SetDefaultDllDirectories_t = BOOL (WINAPI*)(DWORD);
    using AddDllDirectory_t          = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
    using RemoveDllDirectory_t       = BOOL (WINAPI*)(DLL_DIRECTORY_COOKIE);

    HMODULE k = ::GetModuleHandleW(L"kernel32.dll");
    if (!k) return;

    auto pSetDefaultDllDirectories =
        reinterpret_cast<SetDefaultDllDirectories_t>(
            ::GetProcAddress(k, "SetDefaultDllDirectories"));
    auto pAddDllDirectory =
        reinterpret_cast<AddDllDirectory_t>(
            ::GetProcAddress(k, "AddDllDirectory"));

    if (pSetDefaultDllDirectories) {
        // Application dir + System32 + user-added dirs.
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }

    if (pAddDllDirectory && !exeDir.empty()) {
        pAddDllDirectory(exeDir.c_str());
    }

    // If neither API exists (very old OS), we *avoid* calling SetDllDirectory
    // because it disables Safe DLL search for the process. :contentReference[oaicite:10]{index=10}
}

// Make/ensure a directory tree exists (non-throwing).
bool EnsureDir(const fs::path& p) {
    std::error_code ec;
    if (p.empty()) return false;
    if (fs::exists(p, ec)) return !ec;
    fs::create_directories(p, ec);
    return !ec;
}

static fs::path MakeAppHome(const AppId& id, const fs::path& localAppData) {
    fs::path home = localAppData / id.company / id.product;
    EnsureDir(home);
    return home;
}

bool Init(const AppId& app) {
    if (g_inited) return true;

    g_paths.exePath       = GetExecutablePath();
    g_paths.exeDir        = GetExecutableDir();
    g_paths.workingDir    = g_paths.exeDir;

    try {
        g_paths.localAppData   = GetKnownFolder(FOLDERID_LocalAppData);  // e.g. C:\Users\me\AppData\Local
        g_paths.roamingAppData = GetKnownFolder(FOLDERID_RoamingAppData);
        // KNOWN Folders replace legacy CSIDL and are the recommended approach. :contentReference[oaicite:11]{index=11}
    } catch (...) {
        // Fall back to environment if shell isn't available (very rare).
        g_paths.localAppData   = fs::path(::_wgetenv(L"LOCALAPPDATA") ? ::_wgetenv(L"LOCALAPPDATA") : L"");
        g_paths.roamingAppData = fs::path(::_wgetenv(L"APPDATA") ? ::_wgetenv(L"APPDATA") : L"");
    }

    g_paths.tempDir = GetTempFolder();

    // App-scoped dirs (create on demand)
    g_paths.appHome  = MakeAppHome(app, g_paths.localAppData);
    g_paths.logsDir  = g_paths.appHome / L"Logs";   EnsureDir(g_paths.logsDir);
    g_paths.savesDir = g_paths.appHome / L"Saves";  EnsureDir(g_paths.savesDir);
    g_paths.cacheDir = g_paths.appHome / L"Cache";  EnsureDir(g_paths.cacheDir);
    g_paths.configDir= g_paths.appHome / L"Config"; EnsureDir(g_paths.configDir);

    // Make relative asset paths stable (do this before threads start).
    EnsureWorkingDirectoryIsExeDir(g_paths.exeDir);

    // Harden DLL search behavior (where supported).
    HardenDllSearchPath(g_paths.exeDir);

    g_inited = true;
    return true;
}

const AppPaths& Paths() {
    assert(g_inited && "Call winpath::Init(...) first.");
    return g_paths;
}

bool AddDllSearchDir(const fs::path& dirAbs) {
    if (dirAbs.empty() || !dirAbs.is_absolute()) return false;
    using AddDllDirectory_t = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
    HMODULE k = ::GetModuleHandleW(L"kernel32.dll");
    if (!k) return false;
    auto pAddDllDirectory = reinterpret_cast<AddDllDirectory_t>(
        ::GetProcAddress(k, "AddDllDirectory"));
    if (!pAddDllDirectory) return false;
    return pAddDllDirectory(dirAbs.c_str()) != nullptr;
}

} // namespace winpath
