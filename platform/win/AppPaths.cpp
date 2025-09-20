#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj_core.h>   // SHGetKnownFolderPath
#include <filesystem>
#include <string>
#include <cstdlib>
#include <io.h>
#include <fcntl.h>

namespace fs = std::filesystem;

namespace {
    fs::path exe_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return fs::path(std::wstring(buf, n));
    }

    fs::path known_folder(REFKNOWNFOLDERID id) {
        PWSTR w = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w))) {
            fs::path p = w;
            CoTaskMemFree(w);
            return p;
        }
        // Fallback
        if (const wchar_t* env = _wgetenv(L"LOCALAPPDATA")) return fs::path(env);
        return exe_path().parent_path();
    }
}

namespace app::paths {
    std::filesystem::path exe_dir() { return exe_path().parent_path(); }

    std::filesystem::path content_root() {
        const fs::path exe = exe_dir();
        const fs::path candidates[] = {
            exe / L"res",
            exe.parent_path() / L"res"
        };
        for (const auto& c : candidates) {
            std::error_code ec;
            if (fs::exists(c, ec) && fs::is_directory(c, ec)) return c;
        }
        return exe; // last resort
    }

    std::filesystem::path logs_dir() {
        fs::path base = known_folder(FOLDERID_LocalAppData); // %LOCALAPPDATA%
        fs::path logs = base / L"ColonyGame" / L"logs";
        std::error_code ec;
        fs::create_directories(logs, ec);
        return logs;
    }

    bool set_cwd_to_exe() {
        const auto d = exe_dir();
        return SetCurrentDirectoryW(d.c_str()) != 0; // SetCurrentDirectoryW
    }

    void ensure_utf8_console() {
        if (HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); h && h != INVALID_HANDLE_VALUE) {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
            _setmode(_fileno(stdout), _O_U8TEXT);
            _setmode(_fileno(stderr), _O_U8TEXT);
        }
    }
}
