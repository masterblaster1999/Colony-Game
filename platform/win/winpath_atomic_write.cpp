#include "winpath/winpath.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

std::wstring to_wstring(const std::filesystem::path& p) {
    // std::filesystem::path::c_str() is already wide on Windows
    return p.native();
}

bool write_all(HANDLE h, const std::uint8_t* data, std::uint64_t size) {
    std::uint64_t offset = 0;
    while (offset < size) {
        DWORD chunk = (size - offset > 0x7fffffffULL)
                        ? 0x7fffffffUL
                        : static_cast<DWORD>(size - offset);
        DWORD written = 0;
        if (!WriteFile(h, data + offset, chunk, &written, nullptr))
            return false;
        offset += written;
        if (written == 0) return false;
    }
    return true;
}

bool is_retryable_win32_error(DWORD e) noexcept {
    switch (e) {
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_ACCESS_DENIED:
        case ERROR_DELETE_PENDING:
            return true;
        default:
            return false;
    }
}

void sleep_backoff(int attempt) noexcept {
    // 1,2,4,8,16,32,50,50,... ms
    DWORD ms = (attempt < 6) ? (1u << attempt) : 50u;
    Sleep(ms);
}

bool delete_file_retry(const std::filesystem::path& p, int attempts = 32) noexcept {
    if (p.empty()) return true;
    const std::wstring w = to_wstring(p);

    for (int i = 0; i < attempts; ++i) {
        if (DeleteFileW(w.c_str()))
            return true;

        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) return true;

        // Sometimes delete fails because the file is read-only
        if (e == ERROR_ACCESS_DENIED) {
            SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
        }

        if (!is_retryable_win32_error(e))
            return false;

        sleep_backoff(i);
    }
    return false;
}

// Best-effort: flush directory entry for durability.
bool flush_directory(const std::filesystem::path& dir) noexcept {
    if (dir.empty()) return false;

    const std::wstring w = to_wstring(dir);
    HANDLE hDir = CreateFileW(
        w.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (hDir == INVALID_HANDLE_VALUE) return false;

    const bool ok = FlushFileBuffers(hDir) != 0;
    CloseHandle(hDir);
    return ok;
}

} // anon

namespace winpath {

bool atomic_write_file(const std::filesystem::path& dst,
                       const void* data,
                       std::uint64_t size) noexcept
{
    try {
        const auto dir = dst.parent_path();
        std::filesystem::create_directories(dir);

        const std::wstring dstW = to_wstring(dst);

        // Unique temp file in same directory => same volume => replace is atomic-ish.
        static std::atomic_uint32_t s_counter{0};

        std::filesystem::path tmp;
        std::wstring tmpW;

        HANDLE h = INVALID_HANDLE_VALUE;

        for (int attempt = 0; attempt < 32; ++attempt) {
            tmp = dst;
            tmp += L".tmp.";
            tmp += std::to_wstring(GetCurrentProcessId());
            tmp += L".";
            tmp += std::to_wstring(GetCurrentThreadId());
            tmp += L".";
            tmp += std::to_wstring(GetTickCount64());
            tmp += L".";
            tmp += std::to_wstring(++s_counter);

            tmpW = to_wstring(tmp);

            h = CreateFileW(
                tmpW.c_str(),
                GENERIC_WRITE,
                0, // no sharing while writing temp file
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                nullptr
            );

            if (h != INVALID_HANDLE_VALUE) break;

            const DWORD e = GetLastError();
            if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }

        if (h == INVALID_HANDLE_VALUE)
            return false;

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        const bool okWrite = write_all(h, bytes, size);
        const bool okFlush = okWrite && (FlushFileBuffers(h) != 0);
        CloseHandle(h);

        if (!okFlush) {
            (void)delete_file_retry(tmp);
            return false;
        }

        // Retry replace/move on transient sharing violations.
        for (int attempt = 0; attempt < 32; ++attempt) {
            // ReplaceFileW is designed for this pattern. It can fail if the replaced file
            // can't be removed/renamed. :contentReference[oaicite:4]{index=4}
            if (std::filesystem::exists(dst)) {
                if (ReplaceFileW(dstW.c_str(), tmpW.c_str(),
                                 nullptr,
                                 REPLACEFILE_IGNORE_MERGE_ERRORS,
                                 nullptr, nullptr)) {
                    (void)flush_directory(dir);
                    return true;
                }
                const DWORD e = GetLastError();
                if (!is_retryable_win32_error(e))
                    break;
            } else {
                // If dst doesn't exist, MoveFileExW is the right path.
                if (MoveFileExW(tmpW.c_str(), dstW.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    (void)flush_directory(dir);
                    return true;
                }
                const DWORD e = GetLastError();
                if (!is_retryable_win32_error(e))
                    break;
            }

            sleep_backoff(attempt);
        }

        // Final fallback: try MoveFileExW even if dst exists. :contentReference[oaicite:5]{index=5}
        for (int attempt = 0; attempt < 32; ++attempt) {
            if (MoveFileExW(tmpW.c_str(), dstW.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                (void)flush_directory(dir);
                return true;
            }

            const DWORD e = GetLastError();
            if (!is_retryable_win32_error(e))
                break;

            sleep_backoff(attempt);
        }

        // Cleanup temp if replace/move failed.
        (void)delete_file_retry(tmp);
        return false;
    }
    catch (...) {
        return false;
    }
}

} // namespace winpath
