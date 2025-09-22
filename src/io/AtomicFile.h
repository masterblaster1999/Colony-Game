// src/io/AtomicFile.h
//
// Windows-only public API for durable, atomic file writes and fast reads.
// Matches the implementation in src/io/AtomicFileWin.cpp.
//
// Guarantees (Windows, NTFS):
//  - Data is written to a sibling temp file, flushed (FlushFileBuffers), then atomically
//    published over the destination via ReplaceFileW (with optional .bak) or MoveFileExW
//    (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) when creating a new file.
//  - Operations use extended-length paths when needed, so very long paths are supported
//    without the traditional MAX_PATH limit (subject to OS policy).
//
// Build: C++17+, Windows only.

#pragma once

#if !defined(_WIN32)
#  error "cg::io atomic file API is Windows-only in this build."
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace cg::io {

namespace fs = std::filesystem;

// --------------------------------------------------------------------------------------
// Core API (implemented in AtomicFileWin.cpp)
// --------------------------------------------------------------------------------------

/// Atomically write the full contents of `bytes` to `final_path`.
///
/// Implementation details (Windows):
///  - Writes to a temporary sibling file in the same directory.
///  - Calls FlushFileBuffers on the temp file.
///  - If the destination exists, uses ReplaceFileW (optionally creating "<final>.bak").
///    Otherwise falls back to MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH).
///  - Uses extended-length paths internally to avoid MAX_PATH issues.
///
/// @param final_path   Destination path to publish atomically.
/// @param bytes        Entire file contents to write.
/// @param err          Optional: receives a human-readable UTF-8 error on failure.
/// @param make_backup  If true and destination exists, keep a "<final>.bak" backup.
///
/// @return true on success; false on error (with `err` populated if provided).
[[nodiscard]] bool write_atomic(const fs::path& final_path,
                                const std::string& bytes,
                                std::string* err,
                                bool make_backup);

/// Read the entire file at `path` into `out`.
///
/// Implementation details (Windows):
///  - Attempts a memory-mapped read (CreateFileMapping + MapViewOfFile) for speed,
///    falling back to ReadFile on failure.
///  - Opens with read sharing enabled so reads can proceed while other processes write.
///
/// @param path  File to load (must exist).
/// @param out   Destination buffer (replaced on success).
/// @param err   Optional: receives a human-readable UTF-8 error on failure.
///
/// @return true on success; false on error (with `err` populated if provided).
[[nodiscard]] bool read_all(const fs::path& path,
                            std::string& out,
                            std::string* err);

// --------------------------------------------------------------------------------------
// Convenience overloads (header-only). These forward to the canonical core API above.
// They do not require additional .cpp definitions and won’t change your ABI.
// --------------------------------------------------------------------------------------

/// Overload: write from string_view (copies into a std::string once).
[[nodiscard]] inline bool write_atomic(const fs::path& final_path,
                                       std::string_view bytes,
                                       std::string* err = nullptr,
                                       bool make_backup = true)
{
    return write_atomic(final_path, std::string(bytes), err, make_backup);
}

/// Overload: write from raw bytes (copied once).
[[nodiscard]] inline bool write_atomic(const fs::path& final_path,
                                       const void* data,
                                       std::size_t size,
                                       std::string* err = nullptr,
                                       bool make_backup = true)
{
    return write_atomic(final_path,
                        std::string(static_cast<const char*>(data),
                                    static_cast<std::size_t>(size)),
                        err, make_backup);
}

/// Overload: write from vector<uint8_t> (copied once).
[[nodiscard]] inline bool write_atomic(const fs::path& final_path,
                                       const std::vector<std::uint8_t>& bytes,
                                       std::string* err = nullptr,
                                       bool make_backup = true)
{
    return write_atomic(final_path,
                        std::string(reinterpret_cast<const char*>(bytes.data()),
                                    static_cast<std::size_t>(bytes.size())),
                        err, make_backup);
}

/// Overload: read into a vector<uint8_t>.
[[nodiscard]] inline bool read_all(const fs::path& path,
                                   std::vector<std::uint8_t>& out,
                                   std::string* err = nullptr)
{
    std::string tmp;
    if (!read_all(path, tmp, err)) return false;
    out.assign(tmp.begin(), tmp.end());
    return true;
}

// --------------------------------------------------------------------------------------
// Helpers (header-only)
// --------------------------------------------------------------------------------------

/// Return the conventional backup path "<final>.bak" used by write_atomic when
/// `make_backup == true` and the destination exists. Provided for UI/logging.
[[nodiscard]] inline fs::path default_backup_path(const fs::path& final_path)
{
    // Preserve directory; append ".bak" to the filename (after any existing extension).
    // Example: "save.json" -> "save.json.bak"
    fs::path p = final_path;
    p += ".bak";
    return p;
}

/// Light validation helper: check whether a path’s parent directory exists.
/// This mirrors what the implementation does (it will create directories as needed),
/// but is sometimes useful to report better messages up front in UI code.
[[nodiscard]] inline bool parent_exists(const fs::path& p)
{
    std::error_code ec;
    auto parent = p.parent_path().empty() ? fs::current_path(ec) : p.parent_path();
    return fs::exists(parent, ec);
}

} // namespace cg::io
