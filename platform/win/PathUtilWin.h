#pragma once

// Windows path helpers for Colony Game (header-only)
//
// Goals:
// - Keep existing API stable:
//     winpath::exe_path()
//     winpath::exe_dir()
//     winpath::ensure_cwd_exe_dir()
//     winpath::resource_dir()
//     winpath::writable_data_dir()
//     winpath::saved_games_dir()
//     winpath::atomic_write_file()
// - Strengthen correctness (no exceptions required, better fallbacks, safer atomic writes)
// - Add OPTIONAL helpers (do not break existing code):
//     winpath::content_dir()   (env override + upward search for assets/res/resources/Content)
//     winpath::shaders_dir()   (env override + upward search for shaders)

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <cstddef>          // size_t
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

namespace winpath {

namespace detail {

inline std::filesystem::path current_dir_noexcept() {
    std::error_code ec;
    auto p = std::filesystem::current_path(ec);
    if (ec) return {};
    return p;
}

inline bool is_dir_noexcept(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

inline void create_dirs_best_effort(const std::filesystem::path& p) {
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
}

inline std::wstring get_env_wstring(const wchar_t* name) {
    if (!name || !*name) return {};

    // If buffer == nullptr and size == 0, return value is required size incl. null terminator.
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};

    std::wstring value;
    value.resize(static_cast<size_t>(needed));

    const DWORD written = ::GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0) return {};

    value.resize(static_cast<size_t>(written));
    return value;
}

inline std::filesystem::path env_path(const wchar_t* name, const std::filesystem::path& base_dir = {}) {
    const std::wstring v = get_env_wstring(name);
    if (v.empty()) return {};

    std::filesystem::path p(v);
    if (p.is_relative()) {
        // Prefer the provided base_dir; otherwise use current working directory (which the launcher
        // typically sets to exe_dir()).
        const auto base = !base_dir.empty() ? base_dir : current_dir_noexcept();
        if (!base.empty()) p = base / p;
    }
    return p;
}

inline std::filesystem::path first_existing_child_dir(
    const std::filesystem::path& base,
    std::initializer_list<const wchar_t*> names
) {
    if (base.empty()) return {};

    for (const wchar_t* n : names) {
        if (!n || !*n) continue;
        const auto candidate = base / n;
        if (is_dir_noexcept(candidate)) return candidate;
    }
    return {};
}

inline std::filesystem::path first_existing_child_dir_upwards(
    std::filesystem::path start,
    std::initializer_list<const wchar_t*> names,
    int max_depth
) {
    if (start.empty()) return {};
    if (max_depth < 0) return {};

    for (int depth = 0; depth <= max_depth; ++depth) {
        if (const auto hit = first_existing_child_dir(start, names); !hit.empty())
            return hit;

        if (!start.has_parent_path())
            break;

        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return {};
}

inline std::filesystem::path make_unique_tmp_sibling_path(const std::filesystem::path& final_path) {
    const auto parent = final_path.parent_path();
    const auto file   = final_path.filename().wstring();

    std::wstring tmp_name = file;
    tmp_name += L".tmp.";
    tmp_name += std::to_wstring(::GetCurrentProcessId());
    tmp_name += L".";
    tmp_name += std::to_wstring(::GetCurrentThreadId());

    return parent.empty() ? std::filesystem::path(tmp_name) : (parent / tmp_name);
}

inline bool write_all(HANDLE f, const void* data, size_t size_bytes) {
    if (size_bytes == 0) return true;
    if (!data) return false;

    const unsigned char* p = static_cast<const unsigned char*>(data);
    size_t remaining = size_bytes;

    while (remaining > 0) {
        const DWORD chunk = (remaining > static_cast<size_t>(0xFFFFFFFFu))
            ? 0xFFFFFFFFu
            : static_cast<DWORD>(remaining);

        DWORD written = 0;
        if (!::WriteFile(f, p, chunk, &written, nullptr)) return false;
        if (written == 0) return false;

        p += written;
        remaining -= written;
    }
    return true;
}

} // namespace detail

inline std::filesystem::path exe_path() {
    std::vector<wchar_t> buf(1024);
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};

        // If n == buf.size(), the result may be truncated; grow and retry.
        if (n < buf.size()) {
            return std::filesystem::path(buf.data(), buf.data() + n);
        }

        // Grow (cap growth if something is truly wrong).
        if (buf.size() > (1u << 20)) { // ~1M wchar cap: extremely conservative
            return {};
        }
        buf.resize(buf.size() * 2);
    }
}

inline std::filesystem::path exe_dir() {
    auto p = exe_path();
    return p.empty() ? std::filesystem::path{} : p.parent_path();
}

inline void ensure_cwd_exe_dir() {
    const auto dir = exe_dir();
    if (!dir.empty()) {
        ::SetCurrentDirectoryW(dir.c_str());
    }
}

inline std::filesystem::path resource_dir() {
    return exe_dir() / L"res";
}

inline std::filesystem::path writable_data_dir() {
    PWSTR known = nullptr;
    std::filesystem::path out;

    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &known)) && known) {
        out = std::filesystem::path(known) / L"ColonyGame";
        ::CoTaskMemFree(known);
    } else {
        // Fallback (still useful when running in unusual environments)
        const auto base = detail::current_dir_noexcept();
        out = (base.empty() ? exe_dir() : base) / L"ColonyGame";
    }

    detail::create_dirs_best_effort(out);
    return out;
}

inline std::filesystem::path saved_games_dir(const wchar_t* app_name) {
    PWSTR known = nullptr;
    std::filesystem::path out;

    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &known)) && known) {
        out = std::filesystem::path(known) / (app_name ? app_name : L"Colony Game");
        ::CoTaskMemFree(known);
    } else {
        // Fallback: writable app data directory
        out = writable_data_dir() / (app_name ? app_name : L"Colony Game");
    }

    detail::create_dirs_best_effort(out);
    return out;
}

inline bool atomic_write_file(const std::filesystem::path& final_path,
                              const void* data,
                              size_t size_bytes) {
    if (!final_path.has_filename()) return false;

    // Ensure parent directory exists (best effort).
    const auto parent = final_path.parent_path();
    if (!parent.empty()) {
        detail::create_dirs_best_effort(parent);
    }

    const auto tmp = detail::make_unique_tmp_sibling_path(final_path);

    // Create / overwrite temp file in the same directory so that rename is atomic on NTFS.
    HANDLE f = ::CreateFileW(
        tmp.c_str(),
        GENERIC_WRITE,
        0,                 // exclusive while writing
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (f == INVALID_HANDLE_VALUE) return false;

    bool ok = detail::write_all(f, data, size_bytes);

    // Ensure bytes are flushed before we swap the file into place.
    if (ok) {
        ok = (::FlushFileBuffers(f) != 0);
    }

    ::CloseHandle(f);

    if (!ok) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }

    // Best-effort atomic replace:
    // - ReplaceFileW is great when the destination exists.
    // - MoveFileExW handles the "new file" case and is also atomic within a volume.
    if (!::ReplaceFileW(final_path.c_str(), tmp.c_str(), nullptr,
                        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
        if (!::MoveFileExW(tmp.c_str(), final_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ::DeleteFileW(tmp.c_str());
            return false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// OPTIONAL helpers (safe additions; no existing code should break).
// -----------------------------------------------------------------------------

// Finds the "content directory" (assets/res/resources/Content) using:
// 1) COLONY_CONTENT_ROOT (absolute or relative; relative resolved against CWD)
// 2) Upward search from CWD (good for dev builds running from build/ folders)
// 3) Upward search from exe_dir()
inline std::filesystem::path content_dir(int max_depth = 8) {
    // Env override
    if (auto p = detail::env_path(L"COLONY_CONTENT_ROOT"); !p.empty() && detail::is_dir_noexcept(p)) {
        return p;
    }

    constexpr const wchar_t* kNames[] = { L"assets", L"res", L"resources", L"Content" };

    if (const auto cwd = detail::current_dir_noexcept(); !cwd.empty()) {
        if (auto p = detail::first_existing_child_dir_upwards(cwd, { kNames[0], kNames[1], kNames[2], kNames[3] }, max_depth);
            !p.empty()) {
            return p;
        }
    }

    if (const auto exd = exe_dir(); !exd.empty()) {
        if (auto p = detail::first_existing_child_dir_upwards(exd, { kNames[0], kNames[1], kNames[2], kNames[3] }, max_depth);
            !p.empty()) {
            return p;
        }
    }

    return {};
}

// Finds shaders directory using:
// 1) COLONY_SHADERS_ROOT
// 2) Upward search from CWD
// 3) Upward search from exe_dir()
inline std::filesystem::path shaders_dir(int max_depth = 8) {
    // Env override
    if (auto p = detail::env_path(L"COLONY_SHADERS_ROOT"); !p.empty() && detail::is_dir_noexcept(p)) {
        return p;
    }

    constexpr const wchar_t* kNames[] = { L"shaders" };

    if (const auto cwd = detail::current_dir_noexcept(); !cwd.empty()) {
        if (auto p = detail::first_existing_child_dir_upwards(cwd, { kNames[0] }, max_depth); !p.empty()) {
            return p;
        }
    }

    if (const auto exd = exe_dir(); !exd.empty()) {
        if (auto p = detail::first_existing_child_dir_upwards(exd, { kNames[0] }, max_depth); !p.empty()) {
            return p;
        }
    }

    return {};
}

} // namespace winpath
