// platform/win/Paths.cpp
#include "Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <string>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "Shell32.lib") // for SHGetKnownFolderPath

// Some SDKs might not expose these at compile time.
// We load the function dynamically anyway, but define the types/constants to avoid extra headers.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace fs = std::filesystem;

namespace winenv {

// Keep a single source of truth for the app name so logs/user dirs are consistent everywhere.
static std::wstring g_appName = L"ColonyGame";

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static std::wstring module_path_w(HMODULE hmod) {
    // Robust GetModuleFileNameW that grows the buffer if needed (handles long paths).
    DWORD size = 256;
    std::wstring buf(size, L'\0');
    for (;;) {
        DWORD n = ::GetModuleFileNameW(hmod, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            buf.clear();
            break;
        }
        if (n < buf.size() - 1) {
            buf.resize(n);
            break;
        }
        // Buffer was too small; grow it and retry.
        size *= 2;
        buf.assign(size, L'\0');
    }
    return buf;
}

static fs::path from_hmodule(HMODULE hmod) {
    auto full = module_path_w(hmod);
    if (full.empty()) return {};
    return fs::path(full).parent_path();
}

// Lock down DLL search and add the project root as an allowed directory.
// (Best-effort: everything here is dynamically resolved and safe on older OS versions.)
static void secure_dll_search_and_add_root(const fs::path& root) {
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;

    using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
    using PFN_AddDllDirectory        = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);

    auto pSetDefault = reinterpret_cast<PFN_SetDefaultDllDirectories>(
        ::GetProcAddress(k32, "SetDefaultDllDirectories"));
    auto pAddDir = reinterpret_cast<PFN_AddDllDirectory>(
        ::GetProcAddress(k32, "AddDllDirectory"));

    // If available, restrict the default search order to safe locations.
    if (pSetDefault) {
        // Include application dir, system32, and any dirs added via AddDllDirectory.
        pSetDefault(LOAD_LIBRARY_SEARCH_APPLICATION_DIR
                    | LOAD_LIBRARY_SEARCH_SYSTEM32
                    | LOAD_LIBRARY_SEARCH_USER_DIRS);
    }
    // Add our project root (where our bundled DLLs/resources live) if supported.
    if (pAddDir) {
        pAddDir(root.c_str());
    }
}

// Per‑Monitor‑V2 DPI awareness when available (no hard link-time dependency).
static void set_per_monitor_v2_dpi_awareness() {
    HMODULE user32 = ::LoadLibraryW(L"user32.dll");
    if (!user32) return;
    using SetDpiCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setCtx = reinterpret_cast<SetDpiCtxFn>(
        ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx) {
        setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    ::FreeLibrary(user32);
}

// Known folder helper with KF_FLAG_CREATE. Falls back to exe dir on failure.
static fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR out = nullptr;
    fs::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &out))) {
        result = fs::path(out);
        ::CoTaskMemFree(out);
    } else {
        result = from_hmodule(nullptr);
    }
    return result;
}

// ------------------------------------------------------------
// Public API (as declared in Paths.h)
// ------------------------------------------------------------

fs::path exe_dir() {
    return from_hmodule(nullptr);
}

fs::path project_root() {
    fs::path cur = exe_dir();
    // Walk up a few levels to find a "res" folder (dev tree),
    // otherwise fall back to the exe dir (packaged build layout).
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(cur / L"res")) return cur;
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return exe_dir(); // fallback
}

fs::path resource_dir() {
    return project_root() / L"res";
}

fs::path user_data_dir(const std::wstring& appName) {
    return known_folder(FOLDERID_LocalAppData) / appName;
}

void ensure_user_dirs(const std::wstring& appName) {
    auto base = user_data_dir(appName);
    std::error_code ec;
    fs::create_directories(base / L"logs", ec);
    fs::create_directories(base / L"saves", ec);
    fs::create_directories(base / L"crashdumps", ec);
}

void log_debug(const std::wstring& line) {
    ::OutputDebugStringW((line + L"\r\n").c_str());

    const auto logDir = user_data_dir(g_appName) / L"logs";
    std::error_code ec;
    fs::create_directories(logDir, ec);

    const auto logFile = logDir / L"game.log";
    std::wofstream f(logFile, std::ios::app);
    if (f) {
        f << line << L"\n";
    }
}

void init_process_environment(const std::wstring& appName) {
    // Stabilize the app name for user-data and logging across the process.
    g_appName = appName.empty() ? L"ColonyGame" : appName;

    // Set working dir to the project root so relative paths to res/ work both in dev and packaged builds.
    const auto root = project_root();
    if (!root.empty()) {
        ::SetCurrentDirectoryW(root.c_str());
    }

    // Best-practice DLL search hygiene + allow loading from our root dir.
    secure_dll_search_and_add_root(root);

    // Opt-in to modern DPI behavior when available (harmless on older OSes).
    set_per_monitor_v2_dpi_awareness();

    // Ensure user data folders exist ahead of time.
    ensure_user_dirs(g_appName);

    // Friendly check for the resources folder; helps diagnose packaging/mislaunch issues.
    if (!fs::exists(resource_dir())) {
        ::MessageBoxW(nullptr,
            L"Missing resource folder: .\\res\n"
            L"Please run the game from the project root or a properly packaged build.",
            g_appName.c_str(), MB_ICONERROR | MB_OK);
    }

    log_debug(L"Process environment initialized");
}

} // namespace winenv
