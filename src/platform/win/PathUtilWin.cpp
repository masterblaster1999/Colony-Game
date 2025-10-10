#include "platform/win/WinSDK.h"
#include <ShlObj.h>         // SHGetKnownFolderPath
#include <KnownFolders.h>   // FOLDERID_LocalAppData
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <cstdint>
#include <string>
#include "PathUtilWin.h"

namespace fs = std::filesystem;

namespace winpath {

    // --- Helpers ----------------------------------------------------------------

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

    // --- Paths ------------------------------------------------------------------

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

    // --- Atomic write (implementation) ------------------------------------------

    namespace {
        // Core implementation that operates on std::filesystem::path
        bool atomic_write_file_impl(const fs::path& requestedTarget,
                                    const void* data,
                                    size_t size_bytes)
        {
            // Reject invalid buffer when size > 0
            if (!data && size_bytes > 0) {
                return false;
            }

            // Resolve directory; if no parent provided, use exe_dir()
            const fs::path target = requestedTarget;
            const fs::path dir = target.parent_path().empty() ? exe_dir() : target.parent_path();

            // Ensure target directory exists
            std::error_code ec;
            fs::create_directories(dir, ec); // ok if already exists

            // Build a temp filename in the same directory to allow atomic replace
            const std::wstring tmpName =
                L"." + target.filename().wstring() +
                L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                L"_" + std::to_wstring(static_cast<unsigned long long>(GetTickCount64()));

            const fs::path tmp = dir / tmpName;

            // Create temp file for write. Use WRITE_THROUGH so data hits disk on FlushFileBuffers.
            HANDLE h = CreateFileW(
                tmp.c_str(),
                GENERIC_WRITE,
                0,               // no sharing
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr
            );
            if (h == INVALID_HANDLE_VALUE) {
                return false;
            }

            // Write contents (if any) in chunks <= 1 MiB to avoid DWORD overflow
            size_t remaining = size_bytes;
            const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
            constexpr DWORD CHUNK = (1u << 20); // 1 MiB

            while (remaining > 0) {
                const DWORD toWrite = static_cast<DWORD>(std::min<size_t>(remaining, CHUNK));
                DWORD written = 0;
                if (!WriteFile(h, p, toWrite, &written, nullptr) || written != toWrite) {
                    CloseHandle(h);
                    DeleteFileW(tmp.c_str());
                    return false;
                }
                remaining -= written;
                p += written;
            }

            // Ensure data hits disk before replacement
            FlushFileBuffers(h);
            CloseHandle(h);

            // Try atomic replace first (preferred: preserves some metadata/ACLs when possible)
            const DWORD replaceFlags =
                REPLACEFILE_WRITE_THROUGH |
                REPLACEFILE_IGNORE_MERGE_ERRORS |
                REPLACEFILE_IGNORE_ACL_ERRORS;

            if (ReplaceFileW(target.c_str(), tmp.c_str(), nullptr, replaceFlags, nullptr, nullptr)) {
                return true;
            }

            // Fallback: move/rename with replace + write-through (for brand-new files or when ReplaceFileW fails)
            if (MoveFileExW(tmp.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return true;
            }

            // Failure: remove temp file
            DeleteFileW(tmp.c_str());
            return false;
        }
    } // anonymous namespace

    // Atomically write `size_bytes` from `data` into `path` (wstring overload).
    // Strategy: write to a temp file in the same directory, flush, then replace/rename.
    bool atomic_write_file(const std::wstring& path, const void* data, size_t size_bytes) {
        return atomic_write_file_impl(fs::path(path), data, size_bytes);
    }

    // Path overload to support headers that declare the std::filesystem::path signature.
    bool atomic_write_file(const fs::path& target, const void* data, size_t size_bytes) {
        return atomic_write_file_impl(target, data, size_bytes);
    }

} // namespace winpath
