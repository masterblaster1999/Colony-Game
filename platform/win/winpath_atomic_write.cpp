#include "winpath/winpath.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

#ifndef ERROR_UNABLE_TO_REMOVE_REPLACED
#define ERROR_UNABLE_TO_REMOVE_REPLACED 1175L
#endif
#ifndef ERROR_UNABLE_TO_MOVE_REPLACEMENT
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT 1176L
#endif
#ifndef ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 1177L
#endif

std::wstring to_wstring(const std::filesystem::path& p) {
    // std::filesystem::path::native() is already wide on Windows
    return p.native();
}

struct unique_handle {
    HANDLE h = INVALID_HANDLE_VALUE;

    unique_handle() = default;
    explicit unique_handle(HANDLE hh) noexcept : h(hh) {}

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept : h(other.h) {
        other.h = INVALID_HANDLE_VALUE;
    }
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset();
            h = other.h;
            other.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    ~unique_handle() { reset(); }

    void reset(HANDLE hh = INVALID_HANDLE_VALUE) noexcept {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        h = hh;
    }

    HANDLE get() const noexcept { return h; }
    explicit operator bool() const noexcept { return h != INVALID_HANDLE_VALUE; }
};

bool write_all(HANDLE h, const std::uint8_t* data, std::uint64_t size) noexcept {
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::uint64_t remaining = size - offset;
        const DWORD chunk = (remaining > 0x7fffffffULL)
            ? 0x7fffffffUL
            : static_cast<DWORD>(remaining);

        DWORD written = 0;
        if (!WriteFile(h, data + static_cast<std::size_t>(offset), chunk, &written, nullptr)) {
            return false;
        }
        if (written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool is_retryable_error(DWORD e) noexcept {
    switch (e) {
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_ACCESS_DENIED:
        // ReplaceFile-specific transient-ish failures that often boil down to sharing/AV timing:
        case ERROR_UNABLE_TO_REMOVE_REPLACED:        // 1175
        case ERROR_UNABLE_TO_MOVE_REPLACEMENT:      // 1176
        case ERROR_UNABLE_TO_MOVE_REPLACEMENT_2:    // 1177
            return true;
        default:
            return false;
    }
}

void sleep_backoff(DWORD& delay_ms) noexcept {
    Sleep(delay_ms);
    if (delay_ms < 50) delay_ms *= 2;
}

bool delete_file_best_effort(const std::wstring& pathW) noexcept {
    const DWORD start = GetTickCount();
    DWORD delay = 1;

    for (;;) {
        if (DeleteFileW(pathW.c_str())) return true;

        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;

        if (!is_retryable_error(e)) return false;

        if ((GetTickCount() - start) > 2000) return false;
        sleep_backoff(delay);
    }
}

// Wait until the file can be opened with DELETE access.
// This is a practical mitigation for “remove() immediately after write” races on Windows
// (AV/indexers sometimes hold a handle briefly right after rename/replace).
void wait_until_deletable(const std::wstring& pathW) noexcept {
    const DWORD start = GetTickCount();
    DWORD delay = 1;

    for (;;) {
        HANDLE h = CreateFileW(
            pathW.c_str(),
            DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return;
        }

        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return;
        if (!is_retryable_error(e)) return;

        if ((GetTickCount() - start) > 2000) return;
        sleep_backoff(delay);
    }
}

bool replace_or_move_with_retry(const std::wstring& dstW, const std::wstring& tmpW) noexcept {
    const DWORD start = GetTickCount();
    DWORD delay = 1;

    for (;;) {
        // Try ReplaceFileW first (best semantics for “replace contents”).
        if (ReplaceFileW(dstW.c_str(), tmpW.c_str(), nullptr, 0, nullptr, nullptr)) {
            return true;
        }

        DWORD e = GetLastError();

        // Fallback: MoveFileExW can succeed in cases ReplaceFileW rejects.
        if (MoveFileExW(tmpW.c_str(), dstW.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }

        e = GetLastError();

        if (!is_retryable_error(e)) return false;

        if ((GetTickCount() - start) > 2000) return false;
        sleep_backoff(delay);
    }
}

bool create_unique_temp_file(const std::filesystem::path& dst,
                             std::filesystem::path& outTmp,
                             unique_handle& outH)
{
    static std::atomic_uint64_t seq{0};

    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const std::uint64_t t =
        (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
        static_cast<std::uint64_t>(ft.dwLowDateTime);

    // Try a few times in the extremely unlikely event of a collision.
    for (int attempt = 0; attempt < 32; ++attempt) {
        const std::uint64_t s = ++seq;

        std::filesystem::path tmp = dst;
        tmp += L".tmp.";
        tmp += std::to_wstring(GetCurrentProcessId());
        tmp += L".";
        tmp += std::to_wstring(GetCurrentThreadId());
        tmp += L".";
        tmp += std::to_wstring(t);
        tmp += L".";
        tmp += std::to_wstring(s);

        const std::wstring tmpW = to_wstring(tmp);

        HANDLE h = CreateFileW(
            tmpW.c_str(),
            GENERIC_WRITE,
            // Be permissive: avoids us being “the handle that blocks delete” on Windows.
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            CREATE_NEW, // avoid clobbering an existing temp name
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );

        if (h != INVALID_HANDLE_VALUE) {
            outTmp = std::move(tmp);
            outH.reset(h);
            return true;
        }

        const DWORD e = GetLastError();
        if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) {
            continue;
        }
        return false;
    }

    return false;
}

} // namespace

namespace winpath {

bool atomic_write_file(const std::filesystem::path& dst,
                       const void* data,
                       std::uint64_t size) noexcept
{
    try {
        const auto dir = dst.parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec) return false;
        }

        // Create a unique temp file in the same directory (same volume).
        std::filesystem::path tmp;
        unique_handle h;
        if (!create_unique_temp_file(dst, tmp, h)) return false;

        const auto* bytes = static_cast<const std::uint8_t*>(data);

        const bool okWrite = write_all(h.get(), bytes, size);
        const bool okFlush = okWrite && (FlushFileBuffers(h.get()) != 0);

        // Must close before ReplaceFile/MoveFileEx.
        h.reset();

        const std::wstring tmpW = to_wstring(tmp);
        const std::wstring dstW = to_wstring(dst);

        if (!okFlush) {
            (void)delete_file_best_effort(tmpW);
            return false;
        }

        if (!replace_or_move_with_retry(dstW, tmpW)) {
            (void)delete_file_best_effort(tmpW);
            return false;
        }

        // Mitigation for Windows CI flake: ensure the new file is not briefly held
        // by scanners such that an immediate std::filesystem::remove() throws.
        wait_until_deletable(dstW);

        return true;
    }
    catch (...) {
        return false;
    }
}

} // namespace winpath
