#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <string>
#include <system_error>

static void remove_with_retry(const std::filesystem::path& p) {
    std::error_code ec;

    // Try for ~1–3 seconds total with backoff (Windows CI can be briefly “file locked” by scanners).
    for (int attempt = 0; attempt < 64; ++attempt) {
        // Non-throwing overload: clears ec if no error (including "didn't exist").
        std::filesystem::remove(p, ec);
        if (!ec) return; // deleted successfully OR it didn't exist

        // Be tolerant for cleanup: missing path is fine.
        if (ec == std::errc::no_such_file_or_directory) return;

        const DWORD v = static_cast<DWORD>(ec.value()); // MSVC filesystem uses Win32 codes on Windows
        if (v == ERROR_FILE_NOT_FOUND ||
            v == ERROR_PATH_NOT_FOUND)
        {
            return;
        }

        if (v == ERROR_SHARING_VIOLATION ||
            v == ERROR_LOCK_VIOLATION ||
            v == ERROR_ACCESS_DENIED ||
            v == ERROR_DELETE_PENDING)
        {
            // If access denied due to read-only attribute, try clearing it.
            if (v == ERROR_ACCESS_DENIED) {
                const std::wstring w = p.native();
                if (!w.empty()) {
                    SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
                }
            }

            Sleep((attempt < 6) ? (1u << attempt) : 50u);
            ec.clear();
            continue;
        }

        // Non-retryable: throw like filesystem::remove would.
        throw std::filesystem::filesystem_error("remove", p, ec);
    }

    // Still failed after retries: throw for visibility.
    throw std::filesystem::filesystem_error("remove", p, ec);
}

// FIX: prevent C4505 (static function removed as unreferenced under /WX)
[[maybe_unused]] static auto* s_keep_remove_with_retry = &remove_with_retry;

#else
#include <filesystem>
#include <system_error>

static void remove_with_retry(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    if (ec) {
        throw std::filesystem::filesystem_error("remove", p, ec);
    }
}

[[maybe_unused]] static auto* s_keep_remove_with_retry_nonwin = &remove_with_retry;

#endif
