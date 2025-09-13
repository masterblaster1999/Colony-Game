#pragma once
//
// PathUtils.h — Windows-only, header-only utilities for robust path handling.
// C++17, MSVC. Safe for GUI subsystems (no console needed).
//
// Key guarantees & features:
// - All public APIs are wide-character (UTF-16) friendly, returning std::filesystem::path.
// - Robust exe/module path retrieval (long paths supported, dynamic buffer growth).
// - Safe working directory helpers + RAII scope guard.
// - Environment-variable expansion (e.g. "%APPDATA%"), Known Folder access (AppData, ProgramData, Temp).
// - Extended-length path helpers (\\?\… and \\?\UNC\…).
// - Path normalization, canonicalization (weakly), and existence utilities.
// - Search upwards for a file/folder from a starting directory.
// - Filename sanitization and unique path generation.
// - UTF-8 <-> UTF-16 helpers.
// - Keeps compatibility shims for getModulePath(), getModuleDir(), setWorkingDirectoryToModuleDir().
//
// Link requirements (if not using the pragmas below):
//   target_link_libraries(<target> PRIVATE Shell32 Ole32)
//
// This header throws std::runtime_error (PathError) with Windows error text where applicable.
//

#ifndef _WIN32
#  error "PathUtils.h is Windows-only."
#endif

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shlobj.h>        // SHGetKnownFolderPath
#include <knownfolders.h>  // FOLDERID_*
#include <string>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <optional>
#include <algorithm>
#include <cctype>
#include <cwchar>
#include <sstream>
#include <chrono>

#if defined(_MSC_VER)
// If you prefer to link in CMake, remove these two lines:
#  pragma comment(lib, "Shell32.lib")
#  pragma comment(lib, "Ole32.lib")
#endif

namespace pathutils {

// -------- Error type ---------------------------------------------------------

class PathError : public std::runtime_error {
public:
    explicit PathError(const std::string& what) : std::runtime_error(what) {}
};

// Convert a Win32 error code into a UTF-8 message.
[[nodiscard]] inline std::string winErrorMessage(DWORD code) {
    LPWSTR buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD langId = 0;
    DWORD n = ::FormatMessageW(flags, nullptr, code, langId,
                               reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring wmsg = (n && buf) ? std::wstring(buf, buf + n) : L"Unknown error";
    if (buf) ::LocalFree(buf);

    // Trim trailing newlines/spaces commonly returned by FormatMessage.
    while (!wmsg.empty() && (wmsg.back() == L'\r' || wmsg.back() == L'\n' || wmsg.back() == L' '))
        wmsg.pop_back();

    // UTF-16 -> UTF-8
    int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), (int)wmsg.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(bytes, '\0');
    if (bytes > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), (int)wmsg.size(),
                              out.data(), bytes, nullptr, nullptr);
    }
    return out;
}

// Throw PathError from last-error or with custom message prefix.
[[noreturn]] inline void throwLastError(const char* prefix = "Win32 error") {
    DWORD code = ::GetLastError();
    std::ostringstream oss;
    oss << prefix << " (" << code << "): " << winErrorMessage(code);
    throw PathError(oss.str());
}

// -------- UTF-8 / UTF-16 helpers --------------------------------------------

[[nodiscard]] inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int chars = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (chars <= 0) throw PathError("utf8ToWide: invalid UTF-8 input");
    std::wstring out(chars, L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), out.data(), chars);
    return out;
}

[[nodiscard]] inline std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int bytes = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::string();
    std::string out(bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), bytes, nullptr, nullptr);
    return out;
}

// -------- Low-level path retrieval ------------------------------------------

namespace detail {

// Dynamic GetModuleFileNameW with buffer growth. If hMod == nullptr, returns exe path.
[[nodiscard]] inline std::filesystem::path getModulePath_impl(HMODULE hMod) {
    // Start with MAX_PATH; grow as needed.
    DWORD size = 260;
    for (;;) {
        std::wstring buf(size, L'\0');
        DWORD n = ::GetModuleFileNameW(hMod, buf.data(), size);
        if (n == 0) throwLastError("GetModuleFileNameW failed");
        if (n < size) { // not truncated (per docs: equals size if truncated)
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        // Buffer was too small — grow and try again.
        size *= 2;
        if (size > 32768) {
            // 32k chars is the practical upper bound for Win32 paths.
            throw PathError("GetModuleFileNameW: path too long (>32k chars)");
        }
    }
}

} // namespace detail

// Get current process executable path.
[[nodiscard]] inline std::filesystem::path getExecutablePath() {
    return detail::getModulePath_impl(nullptr);
}

// Get path for an arbitrary module (DLL/EXE) handle; pass nullptr for current exe.
[[nodiscard]] inline std::filesystem::path getModulePath(HMODULE hModule) {
    return detail::getModulePath_impl(hModule);
}

// Backwards-compat: same semantics as your original header (returns exe path).
[[nodiscard]] inline std::filesystem::path getModulePath() { // compatibility shim
    return getExecutablePath();
}

// Directory utilities
[[nodiscard]] inline std::filesystem::path getExecutableDir() {
    return getExecutablePath().parent_path();
}

[[nodiscard]] inline std::filesystem::path getModuleDir(HMODULE hModule) {
    return getModulePath(hModule).parent_path();
}

// Backwards-compat: same semantics as your original header (exe dir).
[[nodiscard]] inline std::filesystem::path getModuleDir() { // compatibility shim
    return getExecutableDir();
}

// Current working directory
[[nodiscard]] inline std::filesystem::path getCurrentDirectory() {
    DWORD n = ::GetCurrentDirectoryW(0, nullptr);
    if (n == 0) throwLastError("GetCurrentDirectoryW (size)");
    std::wstring buf(n, L'\0');
    DWORD r = ::GetCurrentDirectoryW(n, buf.data());
    if (r == 0 || r >= n) throwLastError("GetCurrentDirectoryW");
    buf.resize(r);
    return std::filesystem::path(buf);
}

inline void setWorkingDirectory(const std::filesystem::path& p) {
    if (!::SetCurrentDirectoryW(p.c_str())) {
        throwLastError(("SetCurrentDirectoryW: " + wideToUtf8(p.native())).c_str());
    }
}

// Set CWD to the executable's directory (compatibility with your previous API).
inline void setWorkingDirectoryToModuleDir() {
    setWorkingDirectory(getExecutableDir());
}

// RAII guard for temporarily changing the working directory.
class ScopedWorkingDirectory {
    std::filesystem::path saved_;
    bool active_ = false;
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& newDir) {
        saved_  = getCurrentDirectory();
        setWorkingDirectory(newDir);
        active_ = true;
    }
    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory(ScopedWorkingDirectory&& other) noexcept
        : saved_(std::move(other.saved_)), active_(other.active_) { other.active_ = false; }
    ~ScopedWorkingDirectory() {
        if (active_) {
            // Restore; ignore errors in destructor.
            ::SetCurrentDirectoryW(saved_.c_str());
        }
    }
};

// -------- Environment & Known Folders ---------------------------------------

[[nodiscard]] inline std::filesystem::path expandEnvVars(const std::wstring& in) {
    if (in.empty()) return std::filesystem::path();
    DWORD needed = ::ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (needed == 0) throwLastError("ExpandEnvironmentStringsW (size)");
    std::wstring buf(needed, L'\0');
    DWORD written = ::ExpandEnvironmentStringsW(in.c_str(), buf.data(), needed);
    if (written == 0 || written > needed) throwLastError("ExpandEnvironmentStringsW");
    // Exclude trailing null
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return std::filesystem::path(buf);
}

[[nodiscard]] inline std::filesystem::path getTempDirectory() {
    DWORD n = ::GetTempPathW(0, nullptr);
    if (n == 0) throwLastError("GetTempPathW (size)");
    std::wstring buf(n + 1, L'\0');
    DWORD r = ::GetTempPathW((DWORD)buf.size(), buf.data());
    if (r == 0 || r > buf.size()) throwLastError("GetTempPathW");
    // GetTempPathW might leave a trailing backslash; std::filesystem handles it,
    // but we trim the trailing null explicitly.
    while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return std::filesystem::path(buf);
}

// Helper that wraps SHGetKnownFolderPath and returns std::filesystem::path.
[[nodiscard]] inline std::filesystem::path knownFolderPath(REFKNOWNFOLDERID id, DWORD flags = 0) {
    PWSTR raw = nullptr;
    HRESULT hr = ::SHGetKnownFolderPath(id, flags, nullptr, &raw);
    if (FAILED(hr) || !raw) {
        std::ostringstream oss;
        oss << "SHGetKnownFolderPath failed (hr=0x" << std::hex << (unsigned)hr << ")";
        throw PathError(oss.str());
    }
    std::filesystem::path p(raw);
    ::CoTaskMemFree(raw);
    return p;
}

// App data locations (Local, Roaming, ProgramData)
[[nodiscard]] inline std::filesystem::path localAppDataDir() {
    return knownFolderPath(FOLDERID_LocalAppData, 0);
}
[[nodiscard]] inline std::filesystem::path roamingAppDataDir() {
    return knownFolderPath(FOLDERID_RoamingAppData, 0);
}
[[nodiscard]] inline std::filesystem::path programDataDir() {
    return knownFolderPath(FOLDERID_ProgramData, 0);
}

// Build an app-specific data directory: e.g., %LOCALAPPDATA%\Vendor\App
[[nodiscard]] inline std::filesystem::path appDataUnderLocal(const std::wstring& vendor, const std::wstring& app) {
    std::filesystem::path p = localAppDataDir() / vendor / app;
    std::error_code ec;
    std::filesystem::create_directories(p, ec); // ignore if exists
    return p;
}
[[nodiscard]] inline std::filesystem::path appDataUnderRoaming(const std::wstring& vendor, const std::wstring& app) {
    std::filesystem::path p = roamingAppDataDir() / vendor / app;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}
[[nodiscard]] inline std::filesystem::path appDataUnderProgramData(const std::wstring& vendor, const std::wstring& app) {
    std::filesystem::path p = programDataDir() / vendor / app;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

// -------- Extended-length paths & normalization ------------------------------

[[nodiscard]] inline bool isUNCPath(const std::wstring& s) {
    return s.size() >= 2 && s[0] == L'\\' && s[1] == L'\\';
}

[[nodiscard]] inline bool hasExtendedPrefix(const std::wstring& s) {
    // \\?\ or \\?\UNC\
    return s.rfind(L"\\\\?\\", 0) == 0;
}

[[nodiscard]] inline std::wstring addExtendedPrefix(const std::wstring& in) {
    if (in.empty() || hasExtendedPrefix(in)) return in;
    if (isUNCPath(in)) {
        // \\server\share -> \\?\UNC\server\share
        std::wstring out = L"\\\\?\\UNC";
        out.append(in.begin() + 1, in.end()); // drop one backslash
        return out;
    }
    // Drive-absolute: C:\… -> \\?\C:\…
    return L"\\\\?\\" + in;
}

[[nodiscard]] inline std::wstring removeExtendedPrefix(const std::wstring& in) {
    if (!hasExtendedPrefix(in)) return in;
    // \\?\UNC\server\share => \\server\share
    if (in.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        std::wstring out = L"\\";
        out.append(in.begin() + 7, in.end()); // skip "\\?\UNC"
        return out;
    }
    // \\?\C:\… => C:\…
    return std::wstring(in.begin() + 4, in.end());
}

// Normalize slashes to Windows-preferred (backslashes) and optionally add extended prefix.
enum class ExtendedPolicy { AsNeeded, ForceExtended, StripExtended };

[[nodiscard]] inline std::filesystem::path normalizePath(
    const std::filesystem::path& p,
    ExtendedPolicy policy = ExtendedPolicy::AsNeeded,
    bool tryWeaklyCanonical = true)
{
    std::filesystem::path out = p;
    out.make_preferred(); // backslashes on Windows

    if (tryWeaklyCanonical) {
        std::error_code ec;
        auto wc = std::filesystem::weakly_canonical(out, ec);
        if (!ec) out = std::move(wc);
    }

    std::wstring s = out.native();
    if (policy == ExtendedPolicy::ForceExtended) {
        s = addExtendedPrefix(s);
    } else if (policy == ExtendedPolicy::StripExtended) {
        s = removeExtendedPrefix(s);
    }
    return std::filesystem::path(s);
}

// Resolve 'relative' against 'base' (if relative). Returns absolute.
[[nodiscard]] inline std::filesystem::path resolveAgainst(
    const std::filesystem::path& base, const std::filesystem::path& relative)
{
    if (relative.is_absolute()) return relative;
    return std::filesystem::absolute(base / relative);
}

// Is 'child' inside 'parent'? Uses weakly_canonical to handle symlinks where possible.
[[nodiscard]] inline bool isSubpath(const std::filesystem::path& parent, const std::filesystem::path& child) {
    std::error_code ec1, ec2;
    auto p = std::filesystem::weakly_canonical(parent, ec1);
    auto c = std::filesystem::weakly_canonical(child, ec2);
    if (ec1 || ec2) return false;
    auto pit = p.begin(), pend = p.end();
    auto cit = c.begin(), cend = c.end();
    for (; pit != pend; ++pit, ++cit) {
        if (cit == cend || *pit != *cit) return false;
    }
    return true;
}

// -------- Existence, creation, deletion helpers -----------------------------

[[nodiscard]] inline bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}
[[nodiscard]] inline bool dirExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

inline void ensureDirectoryExists(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        if (!std::filesystem::create_directories(p, ec) && ec) {
            throw PathError("create_directories failed: " + wideToUtf8(p.native()) + " : " + ec.message());
        }
    }
}

inline void removeAllNoThrow(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    (void)ec;
}

// Try to atomically replace dst with src (same volume). Fails across volumes.
inline bool atomicReplaceFile(const std::filesystem::path& srcTemp, const std::filesystem::path& finalDst) {
    // MOVEFILE_REPLACE_EXISTING ensures replacement; MOVEFILE_WRITE_THROUGH for durability.
    return ::MoveFileExW(srcTemp.c_str(), finalDst.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// Check writability by attempting to create+delete a temporary file.
[[nodiscard]] inline bool pathIsWritable(const std::filesystem::path& dir) {
    std::filesystem::path probe = dir / L".__probe_write__";
    HANDLE h = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    ::CloseHandle(h);
    // File will be deleted on close.
    return true;
}

// -------- Search upwards for a name -----------------------------------------

// Search from 'start' upwards towards the root for a file or directory named 'targetName'.
// Returns the first match path, or std::nullopt if not found.
[[nodiscard]] inline std::optional<std::filesystem::path> findUpwards(
    std::filesystem::path start, const std::wstring& targetName)
{
    std::error_code ec;
    start = std::filesystem::weakly_canonical(start, ec);
    if (ec) start = std::filesystem::absolute(start);

    for (;;) {
        std::filesystem::path candidate = start / targetName;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = std::move(parent);
    }
    return std::nullopt;
}

// -------- Filename sanitization & unique naming -----------------------------

// Replace characters illegal on Windows ('<', '>', ':', '"', '/', '\\', '|', '?', '*') and control chars.
[[nodiscard]] inline std::wstring sanitizeFileName(const std::wstring& name, wchar_t replacement = L'_') {
    static const std::wstring illegal = L"<>:\"/\\|?*";
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t ch : name) {
        if (ch < 32 || illegal.find(ch) != std::wstring::npos) out.push_back(replacement);
        else out.push_back(ch);
    }
    // Avoid reserved device names.
    auto upper = out;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c){ return (wchar_t)std::towupper(c); });
    static const std::wstring reserved[] = { L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1",L"COM2",L"COM3",L"COM4",L"COM5",L"COM6",L"COM7",L"COM8",L"COM9",
        L"LPT1",L"LPT2",L"LPT3",L"LPT4",L"LPT5",L"LPT6",L"LPT7",L"LPT8",L"LPT9" };
    for (const auto& r : reserved) {
        if (upper == r) { out += L"_"; break; }
    }
    // Trim trailing spaces and dots, which are not allowed for filenames on Win32.
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.empty()) out = L"_";
    return out;
}

// Generate a unique path under 'dir' given base filename and optional extension (without dot).
[[nodiscard]] inline std::filesystem::path makeUniquePath(
    const std::filesystem::path& dir,
    const std::wstring& baseName,
    const std::wstring& extension = L"")
{
    std::wstring safeBase = sanitizeFileName(baseName);
    std::filesystem::path candidate = dir / (safeBase + (extension.empty() ? L"" : (L"." + extension)));
    if (!std::filesystem::exists(candidate)) return candidate;

    // Append (1), (2), ...
    for (int i = 1; i < 1000000; ++i) {
        std::wstringstream ss;
        ss << safeBase << L" (" << i << L")";
        std::filesystem::path p = dir / (ss.str() + (extension.empty() ? L"" : (L"." + extension)));
        if (!std::filesystem::exists(p)) return p;
    }
    // Fallback: timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::wstringstream ss;
    ss << safeBase << L" (" << (long long)t << L")";
    return dir / (ss.str() + (extension.empty() ? L"" : (L"." + extension)));
}

// -------- High-level convenience -------------------------------------------

// Turn a user/path string into an absolute, normalized path:
// 1) Expand %ENV% variables,
// 2) Resolve relative paths against base (or CWD if base empty),
// 3) Normalize (weakly canonical if possible).
[[nodiscard]] inline std::filesystem::path materializePath(
    const std::wstring& userInput,
    const std::optional<std::filesystem::path>& base = std::nullopt,
    ExtendedPolicy ext = ExtendedPolicy::AsNeeded)
{
    auto expanded = expandEnvVars(userInput);
    std::filesystem::path abs = expanded.is_absolute()
        ? expanded
        : resolveAgainst(base ? *base : getCurrentDirectory(), expanded);
    return normalizePath(abs, ext, /*tryWeaklyCanonical=*/true);
}

} // namespace pathutils

// -------------- Public compatibility re-exports (same names as before) ------
//
// If you already include PathUtils.h and call these names in your code,
// they remain available and now benefit from the upgraded implementations.

[[nodiscard]] inline std::filesystem::path getModulePath() {
    return pathutils::getModulePath();
}
[[nodiscard]] inline std::filesystem::path getModuleDir() {
    return pathutils::getModuleDir();
}
inline void setWorkingDirectoryToModuleDir() {
    pathutils::setWorkingDirectoryToModuleDir();
}

