// Windows-only atomic file utilities for Colony Game
// This translation unit upgrades reliability, diagnostics, and performance
// without changing the public API declared in io/AtomicFile.h.
//
// Public API preserved:
//   namespace cg::io {
//     bool write_atomic(const std::filesystem::path& final_path,
//                       const std::string& bytes,
//                       std::string* err /*nullable*/,
//                       bool make_backup);
//     bool read_all(const std::filesystem::path& path,
//                   std::string& out,
//                   std::string* err /*nullable*/);
//   }
//
// Implementation notes (Windows):
//   - Write to a temporary sibling file, FlushFileBuffers, then atomically swap in
//     with ReplaceFileW (when target exists) or MoveFileExW(MOVEFILE_REPLACE_EXISTING |
//     MOVEFILE_WRITE_THROUGH) when creating a new file. This is the canonical Windows
//     approach to atomic replacement. See Microsoft docs for ReplaceFileW, MoveFileExW,
//     and FlushFileBuffers.
//   - Long paths: normalize to \\?\ or \\?\UNC\ (extended-length paths) prior to
//     calling Win32 functions.
//   - Fast read path via memory mapping, fallback to standard I/O.
//
// Build: Windows only, C++17 or newer.

#include "io/AtomicFile.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <fileapi.h>
#include <winbase.h>
#include <handleapi.h>
#include <memoryapi.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <filesystem>
#include <system_error>
#include <random>
#include <fstream>
#include <limits>
#include <type_traits>

namespace fs = std::filesystem;

namespace cg::io {

namespace detail {

// ---------- Small Win32 RAII wrappers ----------

struct ScopedHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : h(handle) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : h(other.h) { other.h = INVALID_HANDLE_VALUE; }
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) { reset(); h = other.h; other.h = INVALID_HANDLE_VALUE; }
        return *this;
    }
    void reset(HANDLE nh = INVALID_HANDLE_VALUE) {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) ::CloseHandle(h);
        h = nh;
    }
    [[nodiscard]] bool valid() const noexcept { return h != INVALID_HANDLE_VALUE && h != nullptr; }
    [[nodiscard]] HANDLE get() const noexcept { return h; }
    [[nodiscard]] HANDLE release() noexcept { HANDLE t = h; h = INVALID_HANDLE_VALUE; return t; }
};

// UTF-8 formatting of last-error
static std::string format_win32_error(DWORD err) {
    LPWSTR msgW = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&msgW), 0, nullptr);
    std::string out;
    if (n && msgW) {
        int bytes = ::WideCharToMultiByte(CP_UTF8, 0, msgW, static_cast<int>(wcslen(msgW)),
                                          nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            out.resize(bytes);
            ::WideCharToMultiByte(CP_UTF8, 0, msgW, static_cast<int>(wcslen(msgW)),
                                  out.data(), bytes, nullptr, nullptr);
        }
    }
    if (msgW) ::LocalFree(msgW);
    if (out.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Win32 error %lu", static_cast<unsigned long>(err));
        out = buf;
    }
    // Trim trailing CR/LF that FormatMessage may include
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

// Normalize path to extended-length form (\\?\ or \\?\UNC\) to avoid MAX_PATH issues.
// This function assumes 'p' refers to a local or UNC path.
static std::wstring to_extended_path(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    const std::wstring raw = abs.native();
    if (raw.size() >= 4 && (raw.rfind(LR"(\\?\)", 0) == 0 || raw.rfind(LR"(\??\)", 0) == 0)) {
        return raw; // already extended
    }
    // UNC path?
    if (raw.size() >= 2 && raw.rfind(LR"(\\)", 0) == 0) {
        // \\server\share\path -> \\?\UNC\server\share\path
        return L"\\\\?\\UNC" + raw.substr(1);
    }
    // Drive-absolute path like C:\...
    return L"\\\\?\\" + raw;
}

// Create directories for a path's parent
static bool ensure_parent_dir(const fs::path& final_path, std::string* err) {
    std::error_code ec;
    const fs::path parent = final_path.parent_path().empty() ? fs::current_path(ec) : final_path.parent_path();
    if (!fs::exists(parent, ec)) {
        if (!fs::create_directories(parent, ec)) {
            if (err) *err = "create_directories failed: " + ec.message();
            return false;
        }
    }
    return true;
}

// Best-effort: clear READONLY on an existing file to reduce ReplaceFile failures.
static void clear_readonly_if_set(const std::wstring& pathW) {
    const DWORD attrs = ::GetFileAttributesW(pathW.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        ::SetFileAttributesW(pathW.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
}

// Generate a unique temporary sibling path in the same directory.
// Format: .<name>.tmp.<pid>_<tick>_<rand>
static fs::path make_temp_sibling(const fs::path& final_path) {
    const DWORD pid = ::GetCurrentProcessId();
    const unsigned long long tick = static_cast<unsigned long long>(::GetTickCount64());

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;

    std::wstring base = final_path.filename().native();
    if (base.empty()) base = L"file";

    wchar_t buf[512];
    std::swprintf(buf, 512, L".%s.tmp.%lu_%llu_%llx",
                  base.c_str(), static_cast<unsigned long>(pid), tick, dist(gen));

    return final_path.parent_path() / buf;
}

// Write entire buffer to an open handle with retries on partial writes.
// Returns true on success; on failure sets err.
static bool write_all_to_handle(HANDLE h, const void* data, size_t size, std::string* err) {
    const BYTE* p = static_cast<const BYTE*>(data);
    size_t left = size;
    while (left > 0) {
        const DWORD chunk = static_cast<DWORD>(left > (1u << 20) ? (1u << 20) : left); // up to 1MB per call
        DWORD wrote = 0;
        if (!::WriteFile(h, p, chunk, &wrote, nullptr)) {
            if (err) *err = "WriteFile failed: " + format_win32_error(::GetLastError());
            return false;
        }
        if (wrote != chunk) {
            if (err) *err = "WriteFile short write";
            return false;
        }
        p += wrote;
        left -= wrote;
    }
    return true;
}

// Optional cross-process mutex to serialize writers on the same final path.
// Not used by default; here for future promotion if desired.
struct ScopedFileMutex {
    ScopedHandle h;
    ScopedFileMutex() = default;
    explicit ScopedFileMutex(const std::wstring& name) {
        // Hash path to keep name under MAX_PATH for object names.
        h.reset(::CreateMutexW(nullptr, FALSE, name.c_str()));
    }
    bool acquired_first_instance() const {
        return h.valid() && ::GetLastError() != ERROR_ALREADY_EXISTS;
    }
};

// Internal streaming writer you can expose later if you wish.
// Current TU-only utility to keep the main API simple.
class AtomicWriter {
public:
    AtomicWriter(const fs::path& final_path, bool write_through, std::string* err_out)
        : final_path_(final_path),
          finalW_(to_extended_path(final_path)),
          tmp_path_(make_temp_sibling(final_path)),
          tmpW_(to_extended_path(tmp_path_)),
          write_through_(write_through) {
        std::wstring dirW = to_extended_path(final_path_.parent_path().empty() ? fs::current_path() : final_path_.parent_path());
        (void)dirW; // left here in case you want to open + flush dir in future

        DWORD flags = FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_SEQUENTIAL_SCAN;
        if (write_through_) flags |= FILE_FLAG_WRITE_THROUGH;

        // GENERIC_WRITE, allow readers to open concurrently (readers will see the *old* file until swap)
        ScopedHandle h(::CreateFileW(tmpW_.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                     nullptr, CREATE_NEW, flags, nullptr));
        if (!h.valid()) {
            if (err_out) *err_out = "CreateFileW(tmp) failed: " + format_win32_error(::GetLastError());
            return;
        }
        file_ = std::move(h);
        ok_ = true;
    }

    bool write(const void* data, size_t size, std::string* err_out) {
        if (!ok_ || !file_.valid()) { if (err_out) *err_out = "AtomicWriter not open"; return false; }
        return write_all_to_handle(file_.get(), data, size, err_out);
    }

    bool write(std::string_view bytes, std::string* err_out) {
        return write(bytes.data(), bytes.size(), err_out);
    }

    // Flush & swap in
    bool commit(bool make_backup, std::string* err_out) {
        if (!ok_ || !file_.valid()) { if (err_out) *err_out = "AtomicWriter not open"; return false; }

        // 1) Flush file contents to disk (best-effort). If this fails we still attempt to publish
        //    the temp file to avoid litter, but we report failure to the caller.
        if (!::FlushFileBuffers(file_.get())) {
            if (err_out) *err_out = "FlushFileBuffers failed: " + format_win32_error(::GetLastError());
            ok_ = false;
        }
        file_.reset(); // close handle before rename

        const DWORD attrs = ::GetFileAttributesW(finalW_.c_str());
        const bool target_exists = (attrs != INVALID_FILE_ATTRIBUTES);

        // If target exists and is readonly, best-effort clear READONLY to avoid ReplaceFile failures.
        if (target_exists && (attrs & FILE_ATTRIBUTE_READONLY)) clear_readonly_if_set(finalW_);

        bool replaced = false;
        std::string replace_error;
        if (target_exists) {
            // Use ReplaceFileW to preserve metadata when possible.
            const std::wstring bak = make_backup ? (finalW_ + L".bak") : L"";

            DWORD flags = REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS;
            if (write_through_) flags |= REPLACEFILE_WRITE_THROUGH;

            if (::ReplaceFileW(finalW_.c_str(), tmpW_.c_str(),
                               make_backup ? bak.c_str() : nullptr, flags,
                               nullptr, nullptr)) {
                replaced = true;
            } else {
                // ReplaceFileW can fail for reasons like missing destination, permissions, or
                // transient locks; we'll fall back to MoveFileExW. Keep the error for diagnostics.
                replace_error = "ReplaceFileW failed: " + format_win32_error(::GetLastError());
            }
        }

        if (!replaced) {
            // Either target doesn't exist or ReplaceFileW failed; try MoveFileExW() replacement.
            DWORD move_flags = MOVEFILE_REPLACE_EXISTING;
            if (write_through_) move_flags |= MOVEFILE_WRITE_THROUGH;

            if (!::MoveFileExW(tmpW_.c_str(), finalW_.c_str(), move_flags)) {
                if (err_out) {
                    *err_out = "MoveFileExW failed: " + format_win32_error(::GetLastError());
                    if (!replace_error.empty()) *err_out += " (after " + replace_error + ")";
                }
                // If rename failed, clean up temp file to avoid litter.
                ::DeleteFileW(tmpW_.c_str());
                return false;
            }
        }

        // Success path: ensure temp is gone (ReplaceFileW already removed it).
        ::DeleteFileW(tmpW_.c_str());

        // If we succeeded and are returning success, ensure the error string is empty even if
        // ReplaceFileW failed and we fell back to MoveFileExW.
        if (ok_ && err_out) err_out->clear();

        return ok_;
    }


    ~AtomicWriter() {
        // Best-effort cleanup if user forgot to commit or commit failed
        if (file_.valid()) file_.reset();
        if (!tmpW_.empty()) ::DeleteFileW(tmpW_.c_str());
    }

private:
    fs::path final_path_;
    std::wstring finalW_;
    fs::path tmp_path_;
    std::wstring tmpW_;
    bool write_through_ = true;
    bool ok_ = false;
    ScopedHandle file_;
};

} // namespace detail

// ---------------- Public API ----------------

bool write_atomic(const fs::path& final_path,
                  const std::string& bytes,
                  std::string* err,
                  bool make_backup)
{
    if (err) err->clear();

    // Ensure parent directory exists
    if (!detail::ensure_parent_dir(final_path, err)) return false;

    // Optional: inter-process writer serialization (disabled by default)
    // const std::wstring lockName = L"Local\\CG_Atomic_" + detail::to_extended_path(final_path);
    // detail::ScopedFileMutex mutex(lockName);

    detail::AtomicWriter writer(final_path, /*write_through=*/true, err);
    if (err && !err->empty()) return false;

    if (!writer.write(std::string_view{bytes}, err)) return false;
    return writer.commit(make_backup, err);
}

bool read_all(const fs::path& path, std::string& out, std::string* err)
{
    if (err) err->clear();
    out.clear();

    const std::wstring pathW = detail::to_extended_path(path);

    // Open for read, allow concurrent read/write/delete so callers can load while game saves
    detail::ScopedHandle h(::CreateFileW(pathW.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h.valid()) {
        if (err) *err = "CreateFileW(open) failed: " + detail::format_win32_error(::GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h.get(), &size)) {
        if (err) *err = "GetFileSizeEx failed: " + detail::format_win32_error(::GetLastError());
        return false;
    }
    if (size.QuadPart < 0) {
        if (err) *err = "Negative file size?";
        return false;
    }
    // Guard against absurdly large files (std::string can't hold > size_t max)
    constexpr unsigned long long kMax = static_cast<unsigned long long>(std::numeric_limits<size_t>::max());
    if (static_cast<unsigned long long>(size.QuadPart) > kMax) {
        if (err) *err = "File too large to read into memory";
        return false;
    }

    // Try memory-mapped fast path
    detail::ScopedHandle mapping(::CreateFileMappingW(h.get(), nullptr, PAGE_READONLY,
                                                      0, 0, nullptr));
    if (mapping.valid()) {
        void* view = ::MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
        if (view) {
            out.assign(static_cast<const char*>(view),
                       static_cast<size_t>(size.QuadPart));
            ::UnmapViewOfFile(view);
            return true;
        }
        // Fall through to standard read on MapView failure
    }

    // Fallback: standard read
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < static_cast<DWORD>(out.size())) {
        DWORD got = 0;
        const BOOL ok = ::ReadFile(h.get(), out.data() + total,
                                   static_cast<DWORD>(out.size()) - total, &got, nullptr);
        if (!ok) {
            if (err) *err = "ReadFile failed: " + detail::format_win32_error(::GetLastError());
            out.clear();
            return false;
        }
        if (got == 0) break; // EOF early
        total += got;
    }
    out.resize(total);
    return true;
}

} // namespace cg::io
