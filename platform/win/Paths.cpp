#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj_core.h>    // SHGetKnownFolderPath
#include <KnownFolders.h>   // FOLDERID_LocalAppData
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace winenv {

static void debug_out(const std::wstring& s) {
    OutputDebugStringW((s + L"\r\n").c_str());
}

void log_debug(const std::wstring& line) {
    debug_out(line);
    // If you later want a file log:
    // auto logDir = user_data_dir() / L"logs";
    // std::error_code ec; fs::create_directories(logDir, ec);
    // std::wofstream(logDir / L"launcher.log", std::ios::app) << line << L"\n";
}

fs::path exe_dir() {
    std::wstring buf(260, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return fs::current_path(); // fallback
        }
        if (n < buf.size() - 1) {     // room for null, not truncated
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);    // grow and retry
    }
    return fs::path(buf).parent_path();
}

static fs::path climb_to_res_root(const fs::path& start) {
    fs::path p = start;
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::exists(p / "res")) return p;
        p = p.parent_path();
    }
    return start; // fallback: no res found; return start
}

fs::path project_root() {
    return climb_to_res_root(exe_dir());
}

fs::path resource_dir() {
    return project_root() / "res";
}

fs::path user_data_dir(const std::wstring& appName) {
    PWSTR raw = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw))) {
        base = fs::path(raw);
        CoTaskMemFree(raw);
    } else {
        base = exe_dir(); // fallback for unusual environments
    }
    return base / appName;
}

static void set_dpi_awareness_per_monitor_v2() {
    // Load dynamically so it also runs on older Windows without hard link errors.
    HMODULE user = LoadLibraryW(L"user32.dll");
    if (!user) return;
    using SetDpiAwarenessContextFn = BOOL (WINAPI*)(HANDLE);
    auto fn = reinterpret_cast<SetDpiAwarenessContextFn>(
        GetProcAddress(user, "SetProcessDpiAwarenessContext"));
    if (fn) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 per headers
        fn(reinterpret_cast<HANDLE>(-4));
    }
    FreeLibrary(user);
}

static void secure_dll_search_and_add_dirs(const fs::path& dir) {
    // Remove current directory from implicit DLL search
    SetDllDirectoryW(L"");

    // SetDefaultDllDirectories if available (Windows 8+)
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    using SetDefaultDllDirsFn = BOOL (WINAPI*)(DWORD);
    auto setDefault = reinterpret_cast<SetDefaultDllDirsFn>(
        GetProcAddress(k32, "SetDefaultDllDirectories"));
    if (setDefault) {
        setDefault(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    }

    // Add our root (or exe) directory explicitly
    HMODULE k32b = GetModuleHandleW(L"kernel32.dll");
    using AddDllDirFn = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
    auto addDir = reinterpret_cast<AddDllDirFn>(
        GetProcAddress(k32b, "AddDllDirectory"));
    if (addDir) {
        addDir(dir.c_str());
    }
}

void ensure_user_dirs(const std::wstring& appName) {
    std::error_code ec;
    auto base = user_data_dir(appName);
    fs::create_directories(base / L"saves", ec);
    fs::create_directories(base / L"logs",  ec);
}

void init_process_environment(const std::wstring& appName) {
    const auto exe   = exe_dir();
    const auto root  = project_root();
    const auto rdir  = resource_dir();

    set_dpi_awareness_per_monitor_v2(); // Improves clarity on high-DPI displays.
    secure_dll_search_and_add_dirs(root);
    SetCurrentDirectoryW(root.c_str()); // make relative asset paths reliable

    if (!fs::exists(rdir) || !fs::is_directory(rdir)) {
        std::wstring msg = L"Colony-Game could not find its 'res' folder.\n\n"
                           L"Tried: " + rdir.wstring() + L"\n\n"
                           L"Make sure the 'res' folder is next to the executable "
                           L"or an ancestor of it.\n";
        MessageBoxW(nullptr, msg.c_str(), L"Missing resources", MB_OK | MB_ICONERROR);
    }

    ensure_user_dirs(appName);
    log_debug(L"[Startup] exe: " + exe.wstring());
    log_debug(L"[Startup] root: " + root.wstring());
    log_debug(L"[Startup] res: "  + rdir.wstring());
}

} // namespace winenv
