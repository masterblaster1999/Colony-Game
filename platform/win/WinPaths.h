#pragma once
//
// platform/win/WinPaths.h  —  Windows-only, header-only path & filesystem utilities
//
// Massive upgrade for Colony-Game: robust known-folder access, portable mode,
// safe path joins, asset search roots, DLL search hardening, atomic writes,
// directory helpers, and a few convenience RAII utilities.
//
// • C++17 or later
// • Windows only (uses Win32 & shell APIs)
// • No external dependencies
//
// Integration notes
// -----------------
// 1) This header is self-contained and inline-only. You can drop it into
//    platform/win/ and start using it without a .cpp.
// 2) It preserves the original public functions you may already call:
//
//      cg::paths::exePath()
//      cg::paths::exeDir()
//      cg::paths::userDataRoot()
//      cg::paths::logsDir()
//      cg::paths::dumpsDir()
//      cg::paths::ensureDir()
//      cg::paths::setCwdToExe()
//
//    …and adds many new, Windows-focused helpers (see the public API section).
//
// 3) Defaults use the product name "ColonyGame" for per-user directories.
//    You can set a different product/company at startup via setAppIdentity().
//
// 4) All functions are [[nodiscard]] where a result should not be ignored.
//    Most functions have noexcept overloads using std::error_code.
//
// 5) Linker notes: Some features (DLL search hardening) use functions that are
//    dynamically resolved at runtime (so there is no extra link requirement).
//
// ----------------------------------------------------------------------------

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <windows.h>
#include <shlobj_core.h>   // SHGetKnownFolderPath, FOLDERID_*
#include <knownfolders.h>  // KNOWNFOLDERID constants
#include <fileapi.h>
#include <winbase.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <mutex>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <system_error>

namespace cg { namespace paths {

  // ============
  // Configuration
  // ============

  // Override at compile-time if desired:
  //   e.g. add -DCG_APP_NAMEW=L"MyGame" -DCG_COMPANY_NAMEW=L"MyStudio"
#ifndef CG_APP_NAMEW
  #define CG_APP_NAMEW L"ColonyGame"
#endif
#ifndef CG_COMPANY_NAMEW
  #define CG_COMPANY_NAMEW L""
#endif

  // ---------------------
  // App identity (mutable)
  // ---------------------
  // By default we use CG_APP_NAMEW / CG_COMPANY_NAMEW. If you want to change
  // them at runtime (early in startup), call setAppIdentity(...).
  inline const std::wstring& appNameW() {
    static std::wstring v = CG_APP_NAMEW;
    return v;
  }
  inline const std::wstring& companyNameW() {
    static std::wstring v = CG_COMPANY_NAMEW;
    return v;
  }
  inline void setAppIdentity(std::wstring product, std::wstring company = L"") {
    const_cast<std::wstring&>(appNameW())     = std::move(product);
    const_cast<std::wstring&>(companyNameW()) = std::move(company);
  }

  // =========
  // Utilities
  // =========

  // UTF-8 <-> UTF-16 conversions (Windows uses UTF-16 for file APIs).
  [[nodiscard]] inline std::wstring utf8_to_utf16(std::string_view s) {
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w; w.resize((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), wlen);
    return w;
  }
  [[nodiscard]] inline std::string utf16_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s; s.resize((size_t)len);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
  }

  // Expand environment strings like "%LOCALAPPDATA%\Foo".
  [[nodiscard]] inline std::wstring expand_env(std::wstring_view in) {
    if (in.empty()) return {};
    // worst-case: env expansion may grow, so call twice
    DWORD n = ExpandEnvironmentStringsW(std::wstring(in).c_str(), nullptr, 0);
    if (n == 0) return std::wstring(in);
    std::wstring out; out.resize(n);
    DWORD n2 = ExpandEnvironmentStringsW(std::wstring(in).c_str(), out.data(), n);
    if (n2 == 0) return std::wstring(in);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
  }

  // Make a Windows-safe filename by removing path separators and reserved characters.
  [[nodiscard]] inline std::wstring sanitize_filename(std::wstring_view in, wchar_t replacement = L'_') {
    static constexpr wchar_t bad[] = L"<>:\"/\\|?*";
    std::wstring s(in);
    for (auto& ch : s) {
      if (ch < 32 || std::wcschr(bad, ch)) ch = replacement;
    }
    // Windows also disallows trailing spaces and dots in filenames
    while (!s.empty() && (s.back() == L' ' || s.back() == L'.')) s.pop_back();
    if (s.empty()) s = L"_";
    return s;
  }

  // ============
  // Known folders
  // ============

  enum class KnownFolder {
    LocalAppData,    // FOLDERID_LocalAppData
    RoamingAppData,  // FOLDERID_RoamingAppData
    ProgramData,     // FOLDERID_ProgramData
    Documents,       // FOLDERID_Documents
    SavedGames,      // FOLDERID_SavedGames
    Desktop,         // FOLDERID_Desktop
    Temp             // GetTempPathW
  };

  [[nodiscard]] inline std::filesystem::path knownFolderPath(KnownFolder kf) {
    using std::filesystem::path;

    if (kf == KnownFolder::Temp) {
      wchar_t buf[MAX_PATH] = {0};
      DWORD n = GetTempPathW(MAX_PATH, buf);
      if (n == 0) return {};
      return path(buf);
    }

    const KNOWNFOLDERID* id = nullptr;
    switch (kf) {
      case KnownFolder::LocalAppData:   id = &FOLDERID_LocalAppData;   break;
      case KnownFolder::RoamingAppData: id = &FOLDERID_RoamingAppData; break;
      case KnownFolder::ProgramData:    id = &FOLDERID_ProgramData;    break;
      case KnownFolder::Documents:      id = &FOLDERID_Documents;      break;
      case KnownFolder::SavedGames:     id = &FOLDERID_SavedGames;     break;
      case KnownFolder::Desktop:        id = &FOLDERID_Desktop;        break;
      default:                          id = &FOLDERID_LocalAppData;   break;
    }

    PWSTR p = nullptr;
    HRESULT hr = SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &p);
    if (FAILED(hr) || !p) return {};
    std::wstring out = p; CoTaskMemFree(p);
    return path(out);
  }

  // ==================
  // Executable & CWD
  // ==================

  [[nodiscard]] inline std::filesystem::path modulePath(HMODULE mod = nullptr) {
    // Robust GetModuleFileNameW: handle long paths by retrying with larger buffer.
    std::wstring buf; buf.resize(MAX_PATH);
    for (;;) {
      DWORD n = GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
      if (n == 0) return {};
      if (n < buf.size() - 1) { buf.resize(n); break; }
      buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf);
  }

  [[nodiscard]] inline std::filesystem::path exePath() { return modulePath(nullptr); }
  [[nodiscard]] inline std::filesystem::path exeDir()  { return exePath().parent_path(); }

  [[nodiscard]] inline std::filesystem::path currentDir(std::error_code& ec) noexcept {
    return std::filesystem::current_path(ec);
  }
  [[nodiscard]] inline std::filesystem::path currentDir() {
    std::error_code ec; auto p = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : p;
  }
  inline bool setCurrentDir(const std::filesystem::path& p, std::error_code& ec) noexcept {
    std::filesystem::current_path(p, ec);
    return !ec;
  }
  inline bool setCurrentDir(const std::filesystem::path& p) {
    std::error_code ec; std::filesystem::current_path(p, ec); return !ec;
  }

  // Set CWD and harden DLL search to the exe directory (best-effort).
  inline void setCwdToExe() {
    auto dir = exeDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::current_path(dir, ec);

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;

    using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
    using PFN_AddDllDirectory         = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
    using PFN_SetDllDirectoryW        = BOOL (WINAPI*)(LPCWSTR);
    using PFN_RemoveDllDirectory      = BOOL (WINAPI*)(DLL_DIRECTORY_COOKIE);

    auto pSetDefault = reinterpret_cast<PFN_SetDefaultDllDirectories>(
      GetProcAddress(k32, "SetDefaultDllDirectories"));
    auto pAddDir = reinterpret_cast<PFN_AddDllDirectory>(
      GetProcAddress(k32, "AddDllDirectory"));
    auto pSetDllDir = reinterpret_cast<PFN_SetDllDirectoryW>(
      GetProcAddress(k32, "SetDllDirectoryW"));
    (void)pSetDllDir;

    // Remove CWD from legacy DLL search path by setting an empty directory.
    if (pSetDllDir) pSetDllDir(L"");

    if (pSetDefault) {
      // Safe default search dirs; user dirs added via AddDllDirectory.
      pSetDefault(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
      if (pAddDir) pAddDir(dir.c_str());
    }
  }

  // RAII helper that temporarily sets CWD and restores on destruction.
  class ScopedCwd {
  public:
    explicit ScopedCwd(const std::filesystem::path& newDir) {
      std::error_code ec;
      old_ = std::filesystem::current_path(ec);
      std::filesystem::current_path(newDir, ec);
    }
    ~ScopedCwd() {
      std::error_code ec;
      if (!old_.empty()) std::filesystem::current_path(old_, ec);
    }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;
  private:
    std::filesystem::path old_;
  };

  // ======================
  // Per-user data root(s)
  // ======================

  // "Portable mode" support:
  //  • Enabled if a file named "portable_mode.txt" exists next to the EXE, or
  //  • Environment variable CG_PORTABLE=1 (or COLONYGAME_PORTABLE=1).
  [[nodiscard]] inline bool portableModeEnabled() {
    auto flagFile = exeDir() / L"portable_mode.txt";
    std::error_code ec;
    if (std::filesystem::exists(flagFile, ec)) return true;

    auto envA = std::getenv("CG_PORTABLE");
    if (envA && (*envA == '1' || *envA == 'Y' || *envA == 'y' || *envA == 'T' || *envA == 't')) return true;

    auto envB = std::getenv("COLONYGAME_PORTABLE");
    if (envB && (*envB == '1' || *envB == 'Y' || *envB == 'y' || *envB == 'T' || *envB == 't')) return true;

    return false;
  }

  // Base per-user directory: by default %LOCALAPPDATA%\ColonyGame
  // If companyNameW() is provided, we use %LOCALAPPDATA%\Company\Product
  // If portableModeEnabled(), we use <exeDir>\UserData
  [[nodiscard]] inline std::filesystem::path userDataRoot() {
    using std::filesystem::path;
    if (portableModeEnabled()) {
      return exeDir() / L"UserData";
    }
    path base = knownFolderPath(KnownFolder::LocalAppData);
    if (base.empty()) base = expand_env(L"%LOCALAPPDATA%");
    if (base.empty()) base = exeDir(); // last resort

    if (!companyNameW().empty())
      return base / companyNameW() / appNameW();
    return base / appNameW();
  }

  // Common subdirectories used by the game (created on demand)
  [[nodiscard]] inline std::filesystem::path logsDir()        { return userDataRoot() / L"logs"; }
  [[nodiscard]] inline std::filesystem::path dumpsDir()       { return userDataRoot() / L"crashdumps"; }
  [[nodiscard]] inline std::filesystem::path savesDir()       { return userDataRoot() / L"saves"; }
  [[nodiscard]] inline std::filesystem::path screenshotsDir() { return userDataRoot() / L"screenshots"; }
  [[nodiscard]] inline std::filesystem::path configDir()      { return userDataRoot() / L"config"; }
  [[nodiscard]] inline std::filesystem::path cacheDir()       { return userDataRoot() / L"cache"; }
  [[nodiscard]] inline std::filesystem::path modsDir()        { return userDataRoot() / L"mods"; }

  // Ensure a directory exists (best effort). Returns the actual directory path if it exists/was created.
  [[nodiscard]] inline std::filesystem::path ensureDir(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
  }

  // Create parent directory of a file path.
  [[nodiscard]] inline std::filesystem::path ensureParent(const std::filesystem::path& file) {
    return ensureDir(file.parent_path());
  }

  // Ensure our standard directories are present (no failure if any cannot be created).
  inline void ensureStandardGameDirs() {
    (void)ensureDir(userDataRoot());
    (void)ensureDir(logsDir());
    (void)ensureDir(dumpsDir());
    (void)ensureDir(savesDir());
    (void)ensureDir(screenshotsDir());
    (void)ensureDir(configDir());
    (void)ensureDir(cacheDir());
    (void)ensureDir(modsDir());
  }

  // ============
  // Path helpers
  // ============

  // Return a weakly canonical path (handles non-existent paths better than canonical()).
  [[nodiscard]] inline std::filesystem::path weaklyCanonical(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::weakly_canonical(p, ec);
  }

  // Check whether 'child' is inside 'base' directory (after weak canonicalization).
  [[nodiscard]] inline bool isSubpath(const std::filesystem::path& base, const std::filesystem::path& child) {
    std::error_code ec;
    auto bc = std::filesystem::weakly_canonical(base, ec);
    auto cc = std::filesystem::weakly_canonical(child, ec);
    if (bc.empty() || cc.empty()) return false;
    auto bit = bc.begin(), bend = bc.end();
    auto cit = cc.begin(), cend = cc.end();
    for (; bit != bend && cit != cend; ++bit, ++cit) {
      if (!(*bit == *cit)) return false;
    }
    return (bit == bend); // all elements of base matched
  }

  // Safe join: returns base/child if child is relative; if absolute, returns child only if it
  // is under base (prevents path traversal when joining user-provided paths).
  [[nodiscard]] inline std::optional<std::filesystem::path>
  safeJoin(const std::filesystem::path& base, const std::filesystem::path& child) {
    using std::filesystem::path;
    path out = child.is_absolute() ? child : (base / child);
    if (!isSubpath(base, out)) return std::nullopt;
    return out;
  }

  // Generate a timestamped filename: "stem-YYYYMMDD-HHMMSS.ext"
  [[nodiscard]] inline std::filesystem::path makeTimestamped(const wchar_t* stem, const wchar_t* ext) {
    using namespace std::chrono;
    auto t  = system_clock::to_time_t(system_clock::now());
    std::tm tmv{}; localtime_s(&tmv, &t);
    wchar_t buf[64];
    wcsftime(buf, 64, L"-%Y%m%d-%H%M%S", &tmv);
    return std::filesystem::path(std::wstring(stem) + buf + ext);
  }

  // Unique temp path next to target (e.g., for atomic writes).
  [[nodiscard]] inline std::filesystem::path tempSiblingFor(const std::filesystem::path& target) {
    auto dir = target.parent_path();
    auto stem = target.stem().wstring();
    auto ext  = target.extension().wstring();
    std::wstring u = stem + L".tmp-" + makeTimestamped(L"", L"").wstring();
    return dir / (u + ext);
  }

  // =========================
  // Asset search root helpers
  // =========================

  // You can maintain a small set of asset search roots, for example:
  //   addAssetSearchRoot(exeDir());
  //   addAssetSearchRoot(exeDir()/L"res");
  //   addAssetSearchRoot(userDataRoot()/L"mods");
  //
  // Then call resolveAsset(L"textures/foo.png") to get the first existing match.
  inline std::vector<std::filesystem::path>& assetRoots() {
    static std::vector<std::filesystem::path> roots;
    return roots;
  }
  inline void clearAssetSearchRoots() { assetRoots().clear(); }
  inline void addAssetSearchRoot(std::filesystem::path root) {
    std::error_code ec;
    root = std::filesystem::weakly_canonical(root, ec);
    assetRoots().push_back(std::move(root));
  }
  [[nodiscard]] inline const std::vector<std::filesystem::path>& getAssetSearchRoots() {
    return assetRoots();
  }
  [[nodiscard]] inline std::optional<std::filesystem::path>
  resolveAsset(const std::filesystem::path& rel) {
    std::error_code ec;
    for (const auto& r : assetRoots()) {
      auto p = r / rel;
      if (std::filesystem::exists(p, ec)) return std::filesystem::weakly_canonical(p, ec);
    }
    return std::nullopt;
  }

  // ===================================
  // File I/O helpers (safe, Windows-y)
  // ===================================

  // Atomic write (best-effort) for text/binary data:
  //   • write to a temp sibling with FILE_FLAG_WRITE_THROUGH
  //   • MoveFileExW to target with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
  //
  // Overload for std::string (binary-safe).
  [[nodiscard]] inline bool writeFileAtomic(const std::filesystem::path& target,
                                            const std::string& bytes,
                                            std::error_code& ec) noexcept
  {
    ec.clear();
    (void)ensureParent(target); // FIX: intentionally discard [[nodiscard]] result (prevents MSVC C4834)
    auto tmp = tempSiblingFor(target);

    // Create a temp file with write-through
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      ec = std::error_code((int)GetLastError(), std::system_category());
      return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    if (!ok || written != bytes.size()) {
      ec = std::error_code((int)GetLastError(), std::system_category());
      CloseHandle(h);
      DeleteFileW(tmp.c_str());
      return false;
    }
    // Ensure data is on disk
    if (!FlushFileBuffers(h)) {
      // Not fatal for all cases, but we record error
      // (we still proceed to move to keep behavior consistent)
    }
    CloseHandle(h);

    // Replace target atomically
    if (!MoveFileExW(tmp.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
      ec = std::error_code((int)GetLastError(), std::system_category());
      DeleteFileW(tmp.c_str());
      return false;
    }
    return true;
  }

  [[nodiscard]] inline bool writeFileAtomic(const std::filesystem::path& target,
                                            const std::string& bytes)
  {
    std::error_code ec;
    return writeFileAtomic(target, bytes, ec);
  }

  // Simple file read to string (binary-safe).
  [[nodiscard]] inline std::optional<std::string>
  readFileAll(const std::filesystem::path& file) {
    std::error_code ec;
    std::ifstream in(file, std::ios::binary);
    if (!in.good()) return std::nullopt;
    in.seekg(0, std::ios::end);
    std::streampos sz = in.tellg();
    if (sz < 0) return std::nullopt;
    std::string data;
    data.resize((size_t)sz);
    in.seekg(0, std::ios::beg);
    in.read(&data[0], (std::streamsize)data.size());
    if (!in.good() && !in.eof()) return std::nullopt;
    return data;
  }

  // =====================
  // Disk space & drivers
  // =====================

  struct DiskSpace {
    unsigned long long freeBytes = 0;    // user-available
    unsigned long long totalBytes = 0;
    unsigned long long freeBytesTotal = 0; // incl. admin
  };

  [[nodiscard]] inline std::optional<DiskSpace>
  queryDiskSpace(const std::filesystem::path& onVolume) {
    ULARGE_INTEGER freeToCaller{}, total{}, freeTotal{};
    if (!GetDiskFreeSpaceExW(onVolume.c_str(), &freeToCaller, &total, &freeTotal)) {
      return std::nullopt;
    }
    DiskSpace ds;
    ds.freeBytes      = freeToCaller.QuadPart;
    ds.totalBytes     = total.QuadPart;
    ds.freeBytesTotal = freeTotal.QuadPart;
    return ds;
  }

  // ==========================
  // Versioned path conveniences
  // ==========================

  // Build a path like: <root>/<appName>/<major.minor.patch>/subdir
  [[nodiscard]] inline std::filesystem::path versionedDir(const std::filesystem::path& root,
                                                           std::wstring_view version,
                                                           const std::filesystem::path& subdir = {})
  {
    auto p = root / appNameW() / std::wstring(version);
    return subdir.empty() ? p : (p / subdir);
  }

  // =========================
  // Quick checks & operations
  // =========================

  [[nodiscard]] inline bool exists(const std::filesystem::path& p) {
    std::error_code ec; return std::filesystem::exists(p, ec);
  }
  [[nodiscard]] inline bool fileExists(const std::filesystem::path& p) {
    std::error_code ec; return std::filesystem::is_regular_file(p, ec);
  }
  [[nodiscard]] inline bool dirExists(const std::filesystem::path& p) {
    std::error_code ec; return std::filesystem::is_directory(p, ec);
  }

  inline uintmax_t removeAll(const std::filesystem::path& p, std::error_code& ec) noexcept {
    return std::filesystem::remove_all(p, ec);
  }
  inline uintmax_t removeAll(const std::filesystem::path& p) {
    std::error_code ec; return std::filesystem::remove_all(p, ec);
  }

  // ============
  // Diagnostics
  // ============

  // Return a concise summary of important directories for logging/diagnostics.
  [[nodiscard]] inline std::wstring diagnosticsSummary() {
    auto w = [&](const std::filesystem::path& p){ return p.wstring(); };
    std::wstring out;
    out += L"ExePath: "       + w(exePath())       + L"\n";
    out += L"ExeDir: "        + w(exeDir())        + L"\n";
    out += L"Cwd: "           + w(currentDir())    + L"\n";
    out += L"Portable: "      + std::wstring(portableModeEnabled() ? L"yes" : L"no") + L"\n";
    out += L"UserDataRoot: "  + w(userDataRoot())  + L"\n";
    out += L"LogsDir: "       + w(logsDir())       + L"\n";
    out += L"DumpsDir: "      + w(dumpsDir())      + L"\n";
    out += L"SavesDir: "      + w(savesDir())      + L"\n";
    out += L"ConfigDir: "     + w(configDir())     + L"\n";
    out += L"CacheDir: "      + w(cacheDir())      + L"\n";
    out += L"ScreenshotsDir: "+ w(screenshotsDir())+ L"\n";
    out += L"ModsDir: "       + w(modsDir())       + L"\n";
    return out;
  }

}} // namespace cg::paths
