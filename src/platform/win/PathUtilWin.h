#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <cstddef>
#include <limits>
#include <system_error>

namespace winpath {

    // ------------------------------------------------------------------------------------
    // Application identity (used to build standard Windows paths)
    // ------------------------------------------------------------------------------------

    // Company/organization name used under %LOCALAPPDATA%\{Company}\...
    // Defaults should be defined in the .cpp; keep functions (not macros) so callers
    // may query dynamically or we can later allow overriding via a setter.
    std::wstring app_company();   // e.g., L"ColonyGame"

    // Product/game name used under Known Folders such as "Saved Games\{Product}"
    std::wstring app_product();   // e.g., L"Colony Game"

    // ------------------------------------------------------------------------------------
    // Executable & working directory
    // ------------------------------------------------------------------------------------

    // Full path to the running .exe
    std::filesystem::path exe_path();

    // Directory of the .exe
    std::filesystem::path exe_dir();

    // Sets CWD to exe_dir (stabilizes relative asset paths regardless of launch context)
    void ensure_cwd_exe_dir();

    // ------------------------------------------------------------------------------------
    // Content & data roots
    // ------------------------------------------------------------------------------------

    // Read-only game assets: <exe_dir>/res
    std::filesystem::path resource_dir();

    // Backwards-compatible writable root:
    // %LOCALAPPDATA%\{Company}  (created if missing)
    // Keep for existing callers; prefer config_dir()/logs_dir() for clarity.
    std::filesystem::path writable_data_dir();

    // Preferred config root:
    // %LOCALAPPDATA%\{Company}
    std::filesystem::path config_dir();

    // Logs folder:
    // %LOCALAPPDATA%\{Company}\logs
    std::filesystem::path logs_dir();

    // Crash dumps folder:
    // %LOCALAPPDATA%\{Company}\crashdumps
    std::filesystem::path crashdump_dir();

    // Saved games folder:
    // Known Folder "Saved Games\{Product}" (FOLDERID_SavedGames). If unavailable, falls back to
    // "Documents\Saved Games\{Product}".
    std::filesystem::path saved_games_dir();

    // Ensure all standard directories exist (config/logs/crashdumps/saved games).
    void ensure_dirs();

    // ------------------------------------------------------------------------------------
    // Atomic file operations (Windows)
    // ------------------------------------------------------------------------------------

    // Writes `size` bytes atomically to `target`. Returns true on success.
    // Implementation writes to a temporary file in the same directory and then
    // replaces the target (e.g., via ReplaceFileW / MoveFileExW).
    //
    // Overload with `out_ec` provides the underlying Win32 failure reason
    // (from GetLastError) as a std::error_code in std::system_category().
    bool atomic_write_file(const std::filesystem::path& target,
                           const void* data,
                           std::size_t size,
                           std::error_code* out_ec) noexcept;

    // Convenience overload (no error details).
    bool atomic_write_file(const std::filesystem::path& target,
                           const void* data,
                           std::size_t size);

    // UTF-8 helper overload for common text payloads.
    inline bool atomic_write_file(const std::filesystem::path& target,
                                  std::string_view utf8) {
        return atomic_write_file(target, utf8.data(), utf8.size());
    }

    // UTF-8 helper overload with error details.
    inline bool atomic_write_file(const std::filesystem::path& target,
                                  std::string_view utf8,
                                  std::error_code* out_ec) noexcept {
        return atomic_write_file(target, utf8.data(), utf8.size(), out_ec);
    }



    // ------------------------------------------------------------------------------------
    // Robust file operations (Windows)
    // ------------------------------------------------------------------------------------
    //
    // Windows file operations can fail transiently due to background scanners (Defender),
    // Explorer preview handlers, or other processes briefly holding handles.
    // These helpers retry a few times with backoff and treat "already gone" as success.

    // Best-effort remove for a file (or empty directory).
    // Returns true if the path was removed OR it did not exist.
    bool remove_with_retry(const std::filesystem::path& path,
                           std::error_code* out_ec = nullptr,
                           int max_attempts = 64) noexcept;

    // Best-effort rename/move.
    // Returns true on success; false on failure.
    bool rename_with_retry(const std::filesystem::path& from,
                           const std::filesystem::path& to,
                           std::error_code* out_ec = nullptr,
                           int max_attempts = 64) noexcept;


    // Best-effort file copy.
    // Returns true on success; false on failure.
    //
    // Notes:
    // - If overwrite_existing is true, an existing destination will be overwritten.
    // - Handles common transient Windows sharing violations by retrying with backoff.
    bool copy_file_with_retry(const std::filesystem::path& from,
                             const std::filesystem::path& to,
                             bool overwrite_existing = true,
                             std::error_code* out_ec = nullptr,
                             int max_attempts = 64) noexcept;


    // Robust read helper: reads a file into a string with retry/backoff for transient locks.
    // This is useful on Windows where background scanners or Explorer can briefly lock files.
    // - Returns true on success, false on failure.
    // - On failure, `out_ec` (if provided) is set to the underlying Win32 reason.
    // - If the file is larger than `max_bytes`, returns false with errc::file_too_large.
    bool read_file_to_string_with_retry(const std::filesystem::path& path,
                                        std::string& out,
                                        std::error_code* out_ec = nullptr,
                                        std::size_t max_bytes = (std::numeric_limits<std::size_t>::max)(),
                                        int max_attempts = 64) noexcept;

} // namespace winpath
