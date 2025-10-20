#include "PathUtilWin.h"
#include <windows.h>
#include <shlobj.h>
#include <vector>

namespace winpath {
    std::filesystem::path exe_path() {
        std::vector<wchar_t> buf(1024);
        DWORD n = 0;
        for (;;) {
            n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
            if (n == 0) return {};
            if (n < buf.size()) break;
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf.data(), buf.data() + n);
    }

    std::filesystem::path exe_dir() {
        auto p = exe_path();
        return p.empty() ? std::filesystem::path{} : p.parent_path();
    }

    void ensure_cwd_exe_dir() {
        const auto dir = exe_dir();
        if (!dir.empty()) SetCurrentDirectoryW(dir.c_str());
    }

    std::filesystem::path resource_dir() {
        return exe_dir() / L"res";
    }

    std::filesystem::path writable_data_dir() {
        PWSTR path = nullptr;
        std::filesystem::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
            out = std::filesystem::path(path) / L"ColonyGame";
            CoTaskMemFree(path);
        }
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
        return out;
    }

    // Optional helpers that were declared in the header:
    std::filesystem::path saved_games_dir(const wchar_t* app_name) {
        PWSTR path = nullptr;
        std::filesystem::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &path))) {
            out = std::filesystem::path(path) / (app_name ? app_name : L"Colony Game");
            CoTaskMemFree(path);
        }
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
        return out;
    }

    bool atomic_write_file(const std::filesystem::path& final_path, const void* data, size_t size_bytes) {
        if (!final_path.has_filename()) return false;
        const auto tmp = final_path.parent_path() / (final_path.filename().wstring() + L".tmp");
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(f, data, (DWORD)size_bytes, &written, nullptr);
        CloseHandle(f);
        if (!ok || written != size_bytes) { DeleteFileW(tmp.c_str()); return false; }
        // ReplaceFileW offers best-effort atomicity on NTFS.
        if (!ReplaceFileW(final_path.c_str(), tmp.c_str(), nullptr, 0, nullptr, nullptr)) {
            // Fallback: rename.
            DeleteFileW(final_path.c_str());
            return MoveFileW(tmp.c_str(), final_path.c_str()) != 0;
        }
        return true;
    }
}
