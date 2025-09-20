#include "Paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>

namespace fs = std::filesystem;
namespace winenv {

static fs::path from_hmodule(HMODULE hmod) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(hmod, buf, MAX_PATH);
    return fs::path(std::wstring(buf, buf + n)).parent_path();
}

fs::path exe_dir() {
    return from_hmodule(nullptr);
}

fs::path project_root() {
    fs::path cur = exe_dir();
    for (int i = 0; i < 6; ++i) {
        if (exists(cur / "res")) return cur;
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return exe_dir(); // fallback
}

fs::path resource_dir() { return project_root() / "res"; }

static fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR out = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &out))) {
        fs::path p(out);
        CoTaskMemFree(out);
        return p;
    }
    return exe_dir();
}

fs::path user_data_dir(const std::wstring& appName) {
    return known_folder(FOLDERID_LocalAppData) / appName;
}

void ensure_user_dirs(const std::wstring& appName) {
    auto base = user_data_dir(appName);
    create_directories(base / "logs");
    create_directories(base / "saves");
    create_directories(base / "crashdumps");
}

void log_debug(const std::wstring& line) {
    OutputDebugStringW((line + L"\r\n").c_str());
    auto logFile = user_data_dir(L"Colony-Game") / "logs" / "game.log";
    std::wofstream(logFile, std::ios::app) << line << L"\n";
}

void init_process_environment(const std::wstring& appName) {
    // Working dir at project root so relative paths to res/ work in dev
    SetCurrentDirectoryW(project_root().c_str());

    // Best-practice DLL search hygiene
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32) {
        using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
        using PFN_AddDllDirectory        = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
        auto pSetDefault = (PFN_SetDefaultDllDirectories)GetProcAddress(k32, "SetDefaultDllDirectories");
        auto pAddDir     = (PFN_AddDllDirectory)       GetProcAddress(k32, "AddDllDirectory");
        if (pSetDefault) pSetDefault(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
        if (pAddDir)     pAddDir(project_root().c_str());
    }

    ensure_user_dirs(appName);
    log_debug(L"Process environment initialized");
}

} // namespace winenv
