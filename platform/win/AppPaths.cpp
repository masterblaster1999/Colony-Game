#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
        // Long-path safe GetModuleFileNameW: grow buffer until it fits.
        std::wstring buf(260, L'\0');
        for (;;) {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) {
                // Best-effort fallback.
                std::error_code ec;
                return fs::current_path(ec);
            }
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            if (buf.size() >= 32768) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return fs::path(buf);
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
