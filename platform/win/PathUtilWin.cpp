#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>         // SHGetKnownFolderPath, FOLDERID_*
#include <filesystem>
#include <system_error>
#include <string>
#include <cwchar>           // swprintf
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

    // ---- %USERPROFILE%\Saved Games\<app_name> (Vista+) -------------------------
    fs::path saved_games_dir(const wchar_t* app_name) {
        PWSTR p = nullptr;
        fs::path base;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &p))) {
            base = p;
            CoTaskMemFree(p);
        } else {
            // Fallback to LocalAppData when Saved Games is unavailable
            PWSTR q = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &q))) {
                base = q;
                CoTaskMemFree(q);
                base /= L"ColonyGame";
            } else {
                base = exe_dir(); // last resort
            }
        }
        fs::path target = base / (app_name ? app_name : L"Colony Game");
        std::error_code ec;
        fs::create_directories(target, ec);
        return target;
    }

    // ---- Atomic write with backup using ReplaceFileW ---------------------------
    // Writes to a temp file on the same volume, flushes, then atomically replaces.
    bool atomic_write_file(const fs::path& final_path,
                           const void* data, size_t size_bytes)
    {
        const fs::path dir  = final_path.parent_path();
        const std::wstring name = final_path.filename().wstring();

        wchar_t tmp_name[64];
        std::swprintf(tmp_name, 64, L".%s.tmp.%u_%llu",
                      name.c_str(), GetCurrentProcessId(),
                      static_cast<unsigned long long>(GetTickCount64()));
        fs::path tmp_path = dir / tmp_name;

        // Create temp with write-through to push bytes to device
        HANDLE h = CreateFileW(tmp_path.c_str(),
                               GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        // Chunked write (handles large buffers)
        const BYTE* p = static_cast<const BYTE*>(data);
        size_t left = size_bytes;
        BOOL ok = TRUE;
        while (left && ok) {
            const DWORD chunk = (left > (1u << 20)) ? (1u << 20) : static_cast<DWORD>(left); // 1MB
            DWORD written = 0;
            ok = WriteFile(h, p, chunk, &written, nullptr);
            if (!ok || written != chunk) break;
            p += chunk; left -= chunk;
        }

        if (ok) ok = FlushFileBuffers(h); // ensure bytes + metadata hit the device
        CloseHandle(h);

        if (!ok) {
            DeleteFileW(tmp_path.c_str());
            return false;
        }

        const fs::path bak_path = final_path.wstring() + L".bak";

        // Try atomic replace with backup first
        if (ReplaceFileW(final_path.c_str(), tmp_path.c_str(), bak_path.c_str(),
                         REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                         nullptr, nullptr)) {
            return true;
        }

        // Fallback: non-atomic “replace existing” move, still write-through
        if (MoveFileExW(tmp_path.c_str(), final_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            // Best-effort: refresh backup
            (void)CopyFileW(final_path.c_str(), bak_path.c_str(), FALSE);
            return true;
        }

        DeleteFileW(tmp_path.c_str());
        return false;
    }

} // namespace winpath
