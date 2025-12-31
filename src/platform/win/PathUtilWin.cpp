#include "platform/win/WinSDK.h"
#include <ShlObj.h>         // SHGetKnownFolderPath
#include <KnownFolders.h>   // FOLDERID_LocalAppData
#include <filesystem>
#include <system_error>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <string>
#include "PathUtilWin.h"

namespace fs = std::filesystem;

namespace winpath {

    // ---------------------------------------------------------------------------------
    // Application identity
    // ---------------------------------------------------------------------------------

    std::wstring app_company()
    {
        // Keep in sync with other path helpers in the repo (e.g. WinFiles/WinPaths).
        // This folder becomes:
        //   %LOCALAPPDATA%\ColonyGame
        return L"ColonyGame";
    }

    std::wstring app_product()
    {
        // Used for "Saved Games\{Product}". Keep it folder-friendly.
        return L"ColonyGame";
    }

    // --- Helpers ----------------------------------------------------------------

    static fs::path known_folder(REFKNOWNFOLDERID id, DWORD flags)
    {
        PWSTR w = nullptr;
        fs::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(id, flags, nullptr, &w))) {
            out = fs::path(w);
            CoTaskMemFree(w);
        }
        return out;
    }

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
        fs::path out;
        if (auto base = known_folder(FOLDERID_LocalAppData, KF_FLAG_CREATE); !base.empty()) {
            out = base / app_company();
        } else {
            // Last-ditch fallback (portable + works in restricted environments).
            out = exe_dir() / L"userdata";
        }
        std::error_code ec;
        fs::create_directories(out, ec);
        return out;
    }

    fs::path config_dir()
    {
        // For now we keep config under the same root as other writable data.
        // If we later split (e.g. Roaming vs Local), this function is the stable API.
        return writable_data_dir();
    }

    fs::path logs_dir()
    {
        fs::path out = config_dir() / L"logs";
        std::error_code ec;
        fs::create_directories(out, ec);
        return out;
    }

    fs::path crashdump_dir()
    {
        fs::path out = config_dir() / L"crashdumps";
        std::error_code ec;
        fs::create_directories(out, ec);
        return out;
    }

    fs::path saved_games_dir()
    {
        // Try the "Saved Games" known folder first.
        fs::path base = known_folder(FOLDERID_SavedGames, KF_FLAG_CREATE);
        if (base.empty())
        {
            // Fallback to Documents\Saved Games
            base = known_folder(FOLDERID_Documents, KF_FLAG_CREATE);
            if (!base.empty())
                base /= L"Saved Games";
        }

        if (base.empty())
        {
            // Final fallback: keep it under config_dir to ensure it's writable.
            base = config_dir();
        }

        fs::path out = base / app_product();
        std::error_code ec;
        fs::create_directories(out, ec);
        return out;
    }

    void ensure_dirs()
    {
        (void)config_dir();
        (void)logs_dir();
        (void)crashdump_dir();
        (void)saved_games_dir();
    }
    // --- Atomic write (implementation) ------------------------------------------

    namespace {

        [[nodiscard]] static bool IsMissingPathError(const std::error_code& ec) noexcept
        {
            if (!ec)
                return false;

            if (ec == std::errc::no_such_file_or_directory)
                return true;

            const DWORD v = static_cast<DWORD>(ec.value());
            return (v == ERROR_FILE_NOT_FOUND || v == ERROR_PATH_NOT_FOUND);
        }

        [[nodiscard]] static bool IsTransientSharingError(const std::error_code& ec) noexcept
        {
            if (!ec)
                return false;

            const DWORD v = static_cast<DWORD>(ec.value());
            return (v == ERROR_SHARING_VIOLATION ||
                    v == ERROR_LOCK_VIOLATION ||
                    v == ERROR_ACCESS_DENIED ||
                    v == ERROR_DELETE_PENDING);
        }

        [[nodiscard]] static bool IsTransientWin32Error(const DWORD v) noexcept
        {
            return (v == ERROR_SHARING_VIOLATION ||
                    v == ERROR_LOCK_VIOLATION ||
                    v == ERROR_ACCESS_DENIED ||
                    v == ERROR_DELETE_PENDING);
        }

        static void ClearReadonlyAttributeBestEffort(const fs::path& p) noexcept
        {
            const std::wstring w = p.native();
            if (w.empty())
                return;

            // If the file is read-only, clearing attributes often allows delete/rename/replace.
            // Ignore failures (best effort).
            (void)::SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
        }

        static void SleepBackoffMs(int attempt) noexcept
        {
            // Exponential backoff for the first few tries, then cap.
            const DWORD ms = (attempt < 6) ? (1u << attempt) : 50u;
            ::Sleep(ms);
        }

        static void SetOutWin32Error(std::error_code* out_ec, DWORD e) noexcept
        {
            if (!out_ec)
                return;

            if (e == 0)
            {
                out_ec->clear();
                return;
            }

            *out_ec = std::error_code(static_cast<int>(e), std::system_category());
        }

        [[nodiscard]] static bool WriteAll(HANDLE h,
                                           const std::uint8_t* data,
                                           std::size_t size_bytes,
                                           DWORD& out_lastErr) noexcept
        {
            out_lastErr = 0;

            std::size_t remaining = size_bytes;
            const std::uint8_t* p = data;

            // Write in chunks <= 1 MiB to avoid DWORD overflow and keep the syscall size sane.
            constexpr DWORD CHUNK = (1u << 20); // 1 MiB

            while (remaining > 0)
            {
                const DWORD toWrite = static_cast<DWORD>(std::min<std::size_t>(remaining, CHUNK));
                DWORD written = 0;

                if (!::WriteFile(h, p, toWrite, &written, nullptr))
                {
                    out_lastErr = ::GetLastError();
                    return false;
                }

                if (written == 0)
                {
                    out_lastErr = ERROR_WRITE_FAULT;
                    return false;
                }

                remaining -= written;
                p += written;
            }

            return true;
        }

        // Core implementation that operates on std::filesystem::path
        bool atomic_write_file_impl(const fs::path& requestedTarget,
                                    const void* data,
                                    std::size_t size_bytes,
                                    std::error_code* out_ec) noexcept
        {
            if (out_ec)
                out_ec->clear();

            if (requestedTarget.empty())
            {
                if (out_ec) *out_ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            // Reject invalid buffer when size > 0
            if (!data && size_bytes > 0)
            {
                if (out_ec) *out_ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            // Resolve directory; if no parent provided, write next to the EXE.
            //
            // NOTE:
            //   We must also resolve the *target path* to that directory.
            //   Otherwise, a relative "settings.json" would be written to exe_dir() (temp file)
            //   but then replaced into the *current working directory* (target), which breaks
            //   atomicity and can silently write to an unexpected location.
            fs::path target = requestedTarget;
            const fs::path dir = target.parent_path().empty() ? exe_dir() : target.parent_path();

            if (target.parent_path().empty() && !dir.empty())
            {
                target = dir / target.filename();
            }

            // Ensure target directory exists (non-throwing).
            std::error_code dec;
            if (!dir.empty())
                fs::create_directories(dir, dec); // ok if already exists

            // Create a unique temp file in the same directory to allow atomic replace.
            // This MUST be unique across threads and rapid successive calls.
            static std::atomic_uint32_t s_tmpCounter{0};

            fs::path tmp;
            HANDLE h = INVALID_HANDLE_VALUE;
            DWORD createErr = 0;

            for (int attempt = 0; attempt < 32; ++attempt)
            {
                const std::uint32_t n = (s_tmpCounter.fetch_add(1, std::memory_order_relaxed) + 1u);

                std::wstring tmpName;
                tmpName.reserve(64 + target.filename().wstring().size());
                tmpName += L".";
                tmpName += target.filename().wstring();
                tmpName += L".tmp.";
                tmpName += std::to_wstring(::GetCurrentProcessId());
                tmpName += L".";
                tmpName += std::to_wstring(::GetCurrentThreadId());
                tmpName += L".";
                tmpName += std::to_wstring(static_cast<unsigned long long>(::GetTickCount64()));
                tmpName += L".";
                tmpName += std::to_wstring(n);

                tmp = dir.empty() ? fs::path(tmpName) : (dir / tmpName);

                // Create temp file for write. Use WRITE_THROUGH so data hits disk on FlushFileBuffers.
                h = ::CreateFileW(
                    tmp.c_str(),
                    GENERIC_WRITE,
                    0,               // no sharing
                    nullptr,
                    CREATE_NEW,       // never clobber a concurrent writer's temp file
                    FILE_ATTRIBUTE_TEMPORARY |
                        FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
                        FILE_FLAG_WRITE_THROUGH |
                        FILE_FLAG_SEQUENTIAL_SCAN,
                    nullptr
                );

                if (h != INVALID_HANDLE_VALUE)
                    break;

                createErr = ::GetLastError();
                if (createErr != ERROR_FILE_EXISTS && createErr != ERROR_ALREADY_EXISTS)
                {
                    SetOutWin32Error(out_ec, createErr);
                    return false;
                }

                // Extremely unlikely, but avoid tight looping if we collided.
                SleepBackoffMs(attempt);
            }

            if (h == INVALID_HANDLE_VALUE)
            {
                SetOutWin32Error(out_ec, createErr ? createErr : ERROR_ALREADY_EXISTS);
                return false;
            }

            DWORD lastErr = 0;
            const bool okWrite = WriteAll(h,
                                         static_cast<const std::uint8_t*>(data),
                                         size_bytes,
                                         lastErr);

            if (!okWrite)
            {
                // Capture error before any other API calls.
                if (lastErr == 0)
                    lastErr = ::GetLastError();

                ::CloseHandle(h);
                (void)winpath::remove_with_retry(tmp);
                SetOutWin32Error(out_ec, lastErr);
                return false;
            }

            // Ensure data hits disk before replacement.
            if (!::FlushFileBuffers(h))
            {
                lastErr = ::GetLastError();
                ::CloseHandle(h);
                (void)winpath::remove_with_retry(tmp);
                SetOutWin32Error(out_ec, lastErr);
                return false;
            }

            ::CloseHandle(h);

            // Retry replace/move to tolerate transient Windows locks (Explorer/Defender).
            const DWORD replaceFlags =
                REPLACEFILE_WRITE_THROUGH |
                REPLACEFILE_IGNORE_MERGE_ERRORS |
                REPLACEFILE_IGNORE_ACL_ERRORS;

            constexpr int kMaxAttempts = 64;

            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                if (::ReplaceFileW(target.c_str(), tmp.c_str(), nullptr, replaceFlags, nullptr, nullptr))
                {
                    return true;
                }

                lastErr = ::GetLastError();

                // MoveFileExW works for both "target exists" and "target missing".
                if (::MoveFileExW(tmp.c_str(), target.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    return true;
                }

                lastErr = ::GetLastError();

                // Sometimes failures are due to read-only destination.
                if (lastErr == ERROR_ACCESS_DENIED)
                    ClearReadonlyAttributeBestEffort(target);

                if (IsTransientWin32Error(lastErr))
                {
                    SleepBackoffMs(attempt);
                    continue;
                }

                break;
            }

            // Failure: remove temp file (best effort).
            (void)winpath::remove_with_retry(tmp);

            SetOutWin32Error(out_ec, lastErr);
            return false;
        }

    } // anonymous namespace

    // Atomically write `size_bytes` from `data` into `target`, returning Win32 error details if requested.
    bool atomic_write_file(const fs::path& target,
                           const void* data,
                           std::size_t size_bytes,
                           std::error_code* out_ec) noexcept
    {
        return atomic_write_file_impl(target, data, size_bytes, out_ec);
    }

    // Convenience overload (no error details).
    bool atomic_write_file(const fs::path& target, const void* data, std::size_t size_bytes)
    {
        return atomic_write_file_impl(target, data, size_bytes, nullptr);
    }

    // --- Robust file ops (Windows) ----------------------------------------------


    bool remove_with_retry(const fs::path& path,
                           std::error_code* out_ec,
                           int max_attempts) noexcept
    {
        if (out_ec) out_ec->clear();
        if (path.empty()) return true;

        std::error_code ec;

        const int attempts = (max_attempts <= 0) ? 1 : max_attempts;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            ec.clear();

            // Non-throwing remove: clears ec on success.
            (void)fs::remove(path, ec);

            if (!ec)
                return true;

            if (IsMissingPathError(ec))
                return true;

            // If access denied, the file might be read-only (common for copied config files).
            if (static_cast<DWORD>(ec.value()) == ERROR_ACCESS_DENIED)
                ClearReadonlyAttributeBestEffort(path);

            if (IsTransientSharingError(ec))
            {
                SleepBackoffMs(attempt);
                continue;
            }

            if (out_ec) *out_ec = ec;
            return false;
        }

        if (out_ec) *out_ec = ec;
        return false;
    }

    bool rename_with_retry(const fs::path& from,
                           const fs::path& to,
                           std::error_code* out_ec,
                           int max_attempts) noexcept
    {
        if (out_ec) out_ec->clear();
        if (from.empty() || to.empty()) return false;

        std::error_code ec;

        const int attempts = (max_attempts <= 0) ? 1 : max_attempts;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            ec.clear();
            fs::rename(from, to, ec);

            if (!ec)
                return true;

            // Missing source isn't retryable.
            if (IsMissingPathError(ec))
            {
                if (out_ec) *out_ec = ec;
                return false;
            }

            // If access denied, clear read-only attribute on both ends best-effort.
            if (static_cast<DWORD>(ec.value()) == ERROR_ACCESS_DENIED)
            {
                ClearReadonlyAttributeBestEffort(from);
                ClearReadonlyAttributeBestEffort(to);
            }

            if (IsTransientSharingError(ec))
            {
                SleepBackoffMs(attempt);
                continue;
            }

            if (out_ec) *out_ec = ec;
            return false;
        }

        if (out_ec) *out_ec = ec;
        return false;
    }



    bool copy_file_with_retry(const fs::path& from,
                             const fs::path& to,
                             bool overwrite_existing,
                             std::error_code* out_ec,
                             int max_attempts) noexcept
    {
        if (out_ec) out_ec->clear();
        if (from.empty() || to.empty()) return false;

        std::error_code ec;

        const int attempts = (max_attempts <= 0) ? 1 : max_attempts;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            ec.clear();

            const fs::copy_options opt = overwrite_existing
                                           ? fs::copy_options::overwrite_existing
                                           : fs::copy_options::none;

            fs::copy_file(from, to, opt, ec);

            if (!ec)
                return true;

            // Missing source isn't retryable.
            if (IsMissingPathError(ec))
            {
                if (out_ec) *out_ec = ec;
                return false;
            }

            // If access denied, clear read-only attribute on both ends best-effort.
            if (static_cast<DWORD>(ec.value()) == ERROR_ACCESS_DENIED)
            {
                ClearReadonlyAttributeBestEffort(from);
                ClearReadonlyAttributeBestEffort(to);
            }

            if (IsTransientSharingError(ec))
            {
                SleepBackoffMs(attempt);
                continue;
            }

            if (out_ec) *out_ec = ec;
            return false;
        }

        if (out_ec) *out_ec = ec;
        return false;
    }



    bool read_file_to_string_with_retry(const fs::path& path,
                                        std::string& out,
                                        std::error_code* out_ec,
                                        std::size_t max_bytes,
                                        int max_attempts) noexcept
    {
        out.clear();
        if (out_ec) out_ec->clear();

        if (path.empty())
        {
            if (out_ec) *out_ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }

        if (max_attempts <= 0)
            max_attempts = 1;

        DWORD lastErr = 0;

        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            lastErr = 0;

            HANDLE h = ::CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr);

            if (h == INVALID_HANDLE_VALUE)
            {
                lastErr = ::GetLastError();

                // Missing file isn't retryable.
                if (lastErr == ERROR_FILE_NOT_FOUND || lastErr == ERROR_PATH_NOT_FOUND)
                {
                    SetOutWin32Error(out_ec, lastErr);
                    return false;
                }

                if (IsTransientWin32Error(lastErr))
                {
                    SleepBackoffMs(attempt);
                    continue;
                }

                SetOutWin32Error(out_ec, lastErr);
                return false;
            }

            LARGE_INTEGER sz{};
            if (!::GetFileSizeEx(h, &sz))
            {
                lastErr = ::GetLastError();
                ::CloseHandle(h);

                if (IsTransientWin32Error(lastErr))
                {
                    SleepBackoffMs(attempt);
                    continue;
                }

                SetOutWin32Error(out_ec, lastErr);
                return false;
            }

            if (sz.QuadPart < 0)
            {
                ::CloseHandle(h);
                if (out_ec) *out_ec = std::make_error_code(std::errc::io_error);
                return false;
            }

            const std::uint64_t u = static_cast<std::uint64_t>(sz.QuadPart);

            if (u > static_cast<std::uint64_t>(max_bytes))
            {
                ::CloseHandle(h);
                if (out_ec) *out_ec = std::make_error_code(std::errc::file_too_large);
                return false;
            }

            const std::size_t size_bytes = static_cast<std::size_t>(u);
            if (size_bytes == 0)
            {
                ::CloseHandle(h);
                out.clear();
                return true;
            }

            out.resize(size_bytes);

            std::size_t offset = 0;
            while (offset < size_bytes)
            {
                constexpr DWORD CHUNK = (1u << 20); // 1 MiB
                const DWORD toRead = static_cast<DWORD>(std::min<std::size_t>(size_bytes - offset, CHUNK));
                DWORD got = 0;
                if (!::ReadFile(h, out.data() + offset, toRead, &got, nullptr))
                {
                    lastErr = ::GetLastError();
                    break;
                }
                if (got == 0)
                {
                    lastErr = ERROR_READ_FAULT;
                    break;
                }
                offset += got;
            }

            ::CloseHandle(h);

            if (offset == size_bytes && lastErr == 0)
            {
                // Success.
                return true;
            }

            // Partial read: treat as failure and retry if transient.
            out.clear();

            if (IsTransientWin32Error(lastErr))
            {
                SleepBackoffMs(attempt);
                continue;
            }

            SetOutWin32Error(out_ec, lastErr);
            return false;
        }

        // Exhausted retries.
        out.clear();
        SetOutWin32Error(out_ec, lastErr);
        return false;
    }


} // namespace winpath
