#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>         // SHGetKnownFolderPath
#include <filesystem>
#include <system_error>
#include "PathUtilWin.h"

namespace fs = std::filesystem;

namespace winpath {

    static std::wstring get_module_path_w() {
        DWORD size = MAX_PATH;
        std::wstring buf(size, L'\0');
        for (;;) {
            DWORD len = GetModuleFileNameW(nullptr, buf.data(), size);
            if (len == 0) return L"";
            if (len < size) {
                buf.resize(len);
                return buf;
            }
            size *= 2;
            buf.resize(size);
        }
    }

    fs::path exe_path() {
        return fs::path(get_module_path_w());
    }

    fs::path exe_dir() {
        fs::path p = exe_path();
        p.remove_filename();
        return p;
    }

    void ensure_cwd_exe_dir() {
        const auto dir = exe_dir();
        if (!dir.empty()) {
            SetCurrentDirectoryW(dir.c_str());
        }
        // (Optional) reduce DLL search hijacking:
        // SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        // SetDllDirectoryW(nullptr);
    }

    fs::path resource_dir() {
        return exe_dir() / L"res";
    }

    fs::path writable_data_dir() {
        PWSTR w = nullptr;
        fs::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &w))) {
            out = fs::path(w) / L"ColonyGame";
            CoTaskMemFree(w);
        } else {
            out = exe_dir() / L"userdata";
        }
        std::error_code ec;
        fs::create_directories(out, ec);
        return out;
    }
}
