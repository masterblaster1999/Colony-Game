#pragma once
//
// CrashDumpWin.h — Windows-only crash dump utilities for Colony-Game
//
// Backwards-compatible (core API in CrashDumpWin.cpp) + power-toolbox (header-only):
//   Core API (do not change; implemented in CrashDumpWin.cpp):
//     • void InstallCrashDumpHandler(const std::wstring& dumpDir, const wchar_t* appName, const wchar_t* appVersion=L"");
//     • bool WriteProcessMiniDump(const std::wstring& dumpDir, const wchar_t* appName, const wchar_t* appVersion=L"", EXCEPTION_POINTERS* info=nullptr);
//
//   Big features added here (header-only):
//     • Dump presets & custom flags builder; inline writer with user streams + callback filter
//     • UserStreamBuilder (UTF-16/UTF-8/JSON/Binary + file capture)
//     • Retention utilities: keep-N and cap-bytes; latest dump discovery
//     • Annotated unhandled filter override (injects crash-keys/system/thread summaries)
//     • Crash keys (global map + RAII scopes) → embedded JSON
//     • System/Thread/Module summaries; sidecar .txt triage writer
//     • Console CTRL handler → best-effort dump on Ctrl+C/Close (console apps)
//     • WER LocalDumps helpers (admin-only; HKLM registry)
//     • Event Log reporter (Application log) to record dump path
//     • NTFS compression toggles for dump folders/files
//     • Symbol path helper for WinDbg/VS (_NT_SYMBOL_PATH)
//     • Rate limiter (windowed) to avoid dump storms
//     • Guard macros (SEH -> dump -> continue), test crash trigger
//
// IMPORTANT:
//   - Requires: Windows + DbgHelp + Shell32 + Advapi32 (your CMake already links these).
//   - See Microsoft docs for MiniDumpWriteDump/MINIDUMP_TYPE/User Streams/Handlers/etc.
//
// --------------------------------------------------------------------------------------------------

#ifndef _WIN32
#  error "CrashDumpWin.h is Windows-only."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <DbgHelp.h>        // MiniDumpWriteDump, MINIDUMP_TYPE, user streams
#include <ShlObj.h>         // SHCreateDirectoryExW, SHGetKnownFolderPath
#include <knownfolders.h>   // FOLDERID_LocalAppData
#include <TlHelp32.h>       // Toolhelp snapshots for threads/modules
#include <winioctl.h>       // FSCTL_SET_COMPRESSION
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <initializer_list>
#include <new>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <csignal>
#include <eh.h>

#if defined(_MSC_VER) && !defined(CRASHDUMPWIN_NO_PRAGMA_LIB)
#  pragma comment(lib, "dbghelp.lib")
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "advapi32.lib")
#  pragma comment(lib, "version.lib")  // optional for file version queries
#endif

// ==================================================================================================
// CORE API (implemented in CrashDumpWin.cpp) — DO NOT ALTER SIGNATURES
// ==================================================================================================
namespace crash {

void InstallCrashDumpHandler(const std::wstring& dumpDir,
                             const wchar_t* appName,
                             const wchar_t* appVersion = L"");

bool WriteProcessMiniDump(const std::wstring& dumpDir,
                          const wchar_t* appName,
                          const wchar_t* appVersion = L"",
                          struct _EXCEPTION_POINTERS* info = nullptr);

} // namespace crash

// ==================================================================================================
// HEADER-ONLY POWER TOOLBOX
// ==================================================================================================
namespace crash {

// ---------- General helpers -----------------------------------------------------------------------
inline bool IsDebuggerAttached() noexcept {
  BOOL remote = FALSE; (void)CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
  return ::IsDebuggerPresent() || remote == TRUE;
}
inline std::wstring JoinPathW(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  const wchar_t sep = L'\\';
  return (a.back() == L'\\' || a.back() == L'/') ? (a + b) : (a + sep + b);
}
inline bool EnsureDirectoryTree(const std::wstring& path) {
  if (path.empty()) return false;
  const int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
  return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}
inline std::wstring GetExecutablePathW() {
  std::wstring buf(260, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (n == 0) return L"";
    if (n < buf.size() - 1) { buf.resize(n); return buf; }
    buf.resize(buf.size() * 2);
  }
}
inline std::wstring GetExecutableDirW() {
  auto p = GetExecutablePathW(); if (p.empty()) return L".";
  size_t pos = p.find_last_of(L"\\/");
  return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}
inline std::wstring DefaultDumpDirNearExe() {
  return JoinPathW(GetExecutableDirW(), L"crash_dumps");
}
inline std::wstring DefaultDumpDirLocalAppData(const wchar_t* company = L"Colony-Game",
                                               const wchar_t* product = L"ColonyGame") {
  PWSTR wz = nullptr; std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &wz)) && wz) {
    out = wz; CoTaskMemFree(wz);
  } else {
    out = DefaultDumpDirNearExe();
  }
  out = JoinPathW(out, company);
  out = JoinPathW(out, product);
  return JoinPathW(out, L"CrashDumps");
}
inline std::wstring SanitizeFilenameW(std::wstring s) {
  static constexpr wchar_t bad[] = L"<>:\"/\\|?*\t\r\n";
  for (auto& ch : s) for (wchar_t b : bad) if (ch == b) { ch = L'_'; break; }
  return s;
}
inline std::wstring NowUtcTimestampW() {
  SYSTEMTIME st{}; GetSystemTime(&st);
  wchar_t ts[32]; swprintf(ts, 32, L"%04u%02u%02uT%02u%02u%02uZ",
                           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return ts;
}
inline std::wstring MakeDumpFileNameW(const wchar_t* appName,
                                      const wchar_t* appVersion,
                                      DWORD pid = GetCurrentProcessId(),
                                      const std::wstring& ts = NowUtcTimestampW()) {
  const std::wstring a = SanitizeFilenameW(appName && *appName ? appName : L"App");
  const std::wstring v = (appVersion && *appVersion) ? (std::wstring(L"-") + SanitizeFilenameW(appVersion)) : L"";
  wchar_t pidbuf[16]; swprintf(pidbuf, 16, L"-%u", pid);
  return a + v + L"-" + ts + pidbuf + L".dmp";
}
inline std::wstring GetLastErrorMessageW(DWORD code = GetLastError()) {
  LPWSTR msg = nullptr; DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = FormatMessageW(flags, nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
  std::wstring s = (len && msg) ? std::wstring(msg, len) : L"";
  if (msg) LocalFree(msg);
  return s;
}

// ---------- Retention & discovery -----------------------------------------------------------------
struct DumpInfo { std::wstring path; uint64_t size_bytes = 0; FILETIME last_write{}; };
inline std::vector<DumpInfo> EnumerateDumps(const std::wstring& dir) {
  std::vector<DumpInfo> out; WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((JoinPathW(dir, L"*.dmp")).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      const uint64_t size = (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
      out.push_back({ JoinPathW(dir, fd.cFileName), size, fd.ftLastWriteTime });
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end(), [](const DumpInfo& a, const DumpInfo& b) {
    const auto la = (uint64_t(a.last_write.dwHighDateTime) << 32) | a.last_write.dwLowDateTime;
    const auto lb = (uint64_t(b.last_write.dwHighDateTime) << 32) | b.last_write.dwLowDateTime;
    return la > lb; // newest first
  });
  return out;
}
inline void PruneByNewestN(const std::wstring& dir, size_t keepNewestN) {
  if (!keepNewestN) return; auto list = EnumerateDumps(dir);
  for (size_t i = keepNewestN; i < list.size(); ++i) (void)DeleteFileW(list[i].path.c_str());
}
inline void PruneByMaxBytes(const std::wstring& dir, uint64_t maxBytes) {
  if (!maxBytes) return; auto list = EnumerateDumps(dir);
  uint64_t acc = 0;
  for (size_t i = 0; i < list.size(); ++i) { acc += list[i].size_bytes; if (acc > maxBytes) (void)DeleteFileW(list[i].path.c_str()); }
}
inline std::wstring LatestDumpPath(const std::wstring& dir) {
  auto list = EnumerateDumps(dir); return list.empty() ? L"" : list.front().path;
}

// ---------- Presets & flags (MINIDUMP_TYPE) -------------------------------------------------------
// MiniDumpWriteDump + MINIDUMP_TYPE are documented on Microsoft Learn. :contentReference[oaicite:1]{index=1}
enum class DumpPreset : uint32_t { Tiny, Small, Medium, FullMemory, Custom };
inline MINIDUMP_TYPE FlagsForPreset(DumpPreset p) {
  switch (p) {
    case DumpPreset::Tiny:       return MINIDUMP_TYPE(MiniDumpNormal);
    case DumpPreset::Small:      return MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
    case DumpPreset::Medium:     return MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory);
    case DumpPreset::FullMemory: return MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithThreadInfo | MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData);
    case DumpPreset::Custom: default: return MINIDUMP_TYPE(MiniDumpNormal);
  }
}
inline MINIDUMP_TYPE FlagsForPresetCustom(bool withPrivateRW = false,
                                          bool withoutOptional = false,
                                          bool withCodeSegs = false,
                                          bool withModuleHeaders = false) {
  MINIDUMP_TYPE t = FlagsForPreset(DumpPreset::Medium);
  if (withPrivateRW)   t = MINIDUMP_TYPE(t | MiniDumpWithPrivateReadWriteMemory);
  if (withoutOptional) t = MINIDUMP_TYPE(t | MiniDumpWithoutOptionalData);
  if (withCodeSegs)    t = MINIDUMP_TYPE(t | MiniDumpWithCodeSegs);
  if (withModuleHeaders) t = MINIDUMP_TYPE(t | MiniDumpWithModuleHeaders);
  return t;
}
inline DumpPreset PresetForDebugger() { return IsDebuggerAttached() ? DumpPreset::FullMemory : DumpPreset::Medium; }

// ---------- User streams (embed extra data) -------------------------------------------------------
// MINIDUMP_USER_STREAM + MINIDUMP_USER_STREAM_INFORMATION: :contentReference[oaicite:2]{index=2}
class UserStreamBuilder {
public:
  UserStreamBuilder() = default;

  UserStreamBuilder& AddTextW(const std::wstring& text, ULONG32 type = CommentStreamW) {
    Owned o; o.buf.resize((text.size() + 1) * sizeof(wchar_t));
    memcpy(o.buf.data(), text.c_str(), o.buf.size());
    o.stream.Type = type; o.stream.Buffer = o.buf.data(); o.stream.BufferSize = (ULONG)o.buf.size();
    owned_.push_back(std::move(o)); return *this;
  }
  UserStreamBuilder& AddTextA(const std::string& text, ULONG32 type = CommentStreamA) {
    Owned o; o.buf.assign(text.begin(), text.end()); o.buf.push_back('\0');
    o.stream.Type = type; o.stream.Buffer = o.buf.data(); o.stream.BufferSize = (ULONG)o.buf.size();
    owned_.push_back(std::move(o)); return *this;
  }
  UserStreamBuilder& AddJson(const std::string& jsonUtf8) { return AddTextA(jsonUtf8, CommentStreamA); }
  UserStreamBuilder& AddBinary(const void* data, size_t bytes, ULONG32 customType) {
    Owned o; o.buf.resize(bytes); if (bytes) memcpy(o.buf.data(), data, bytes);
    o.stream.Type = customType; o.stream.Buffer = o.buf.data(); o.stream.BufferSize = (ULONG)o.buf.size();
    owned_.push_back(std::move(o)); return *this;
  }
  UserStreamBuilder& AddTextFileA(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return *this;
    std::vector<char> buf; char tmp[4096]; size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    fclose(f); return AddTextA(std::string(buf.begin(), buf.end()));
  }
  UserStreamBuilder& AddKeyValuesJson(std::initializer_list<std::pair<std::string,std::string>> kv) {
    std::string j = "{"; bool first = true;
    for (auto& p : kv) { if (!first) j += ','; first = false; j += "\"" + p.first + "\":\"" + p.second + "\""; }
    j += "}"; return AddJson(j);
  }
  MINIDUMP_USER_STREAM_INFORMATION Build() {
    streams_.clear(); streams_.reserve(owned_.size());
    for (auto& o : owned_) streams_.push_back(o.stream);
    MINIDUMP_USER_STREAM_INFORMATION info{};
    info.UserStreamCount = (ULONG)streams_.size();
    info.UserStreamArray = streams_.empty() ? nullptr : streams_.data();
    return info;
  }

private:
  struct Owned { std::vector<uint8_t> buf; MINIDUMP_USER_STREAM stream{}; };
  std::vector<Owned> owned_;
  std::vector<MINIDUMP_USER_STREAM> streams_;
};

// ---------- Callback filter (module/memory triage) ------------------------------------------------
// Vectored/SEH/Callback docs: :contentReference[oaicite:3]{index=3}
struct CallbackFilter {
  std::vector<std::wstring> exclude_module_substrings; // case-insensitive
  size_t approximate_memory_bytes_limit = 0;           // advisory cap (0=unlimited)
  // Optional: include-only virtual address ranges (pairs of [base,size]); empty = all allowed.
  std::vector<std::pair<ULONG64, ULONG64>> include_address_ranges;
};
inline BOOL __stdcall MiniDumpCallbackFilter(PVOID param, const PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT out) {
  const CallbackFilter* f = reinterpret_cast<const CallbackFilter*>(param);
  if (!in || !out) return TRUE;
  switch (in->CallbackType) {
    case ModuleCallback:
      if (f && !f->exclude_module_substrings.empty() && in->Module.FullPath) {
        std::wstring path = in->Module.FullPath;
        std::transform(path.begin(), path.end(), path.begin(), ::towlower);
        for (const auto& sub : f->exclude_module_substrings) {
          std::wstring low = sub; std::transform(low.begin(), low.end(), low.begin(), ::towlower);
          if (path.find(low) != std::wstring::npos) return FALSE;
        }
      }
      break;
    case MemoryCallback:
      if (f) {
        if (f->approximate_memory_bytes_limit && in->Memory.Size > f->approximate_memory_bytes_limit)
          return FALSE;
        if (!f->include_address_ranges.empty()) {
          ULONG64 a = in->Memory.BaseAddress, sz = in->Memory.Size;
          bool ok = false;
          for (auto& r : f->include_address_ranges) {
            ULONG64 lo = r.first, hi = r.first + r.second;
            if (a >= lo && (a + sz) <= hi) { ok = true; break; }
          }
          if (!ok) return FALSE;
        }
      }
      break;
    default: break;
  }
  return TRUE;
}

// ---------- Inline writer wrappers ----------------------------------------------------------------
// MiniDumpWriteDump signature & params: :contentReference[oaicite:4]{index=4}
inline bool WriteMiniDumpToFile(const std::wstring& fullPath,
                                MINIDUMP_TYPE flags,
                                EXCEPTION_POINTERS* ep = nullptr,
                                const MINIDUMP_USER_STREAM_INFORMATION* user = nullptr,
                                const MINIDUMP_CALLBACK_INFORMATION* cb   = nullptr,
                                HANDLE process = GetCurrentProcess(),
                                DWORD  pid     = GetCurrentProcessId()) {
  HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;

  MINIDUMP_EXCEPTION_INFORMATION mei{}; PMINIDUMP_EXCEPTION_INFORMATION pMei = nullptr;
  if (ep) { mei.ThreadId = GetCurrentThreadId(); mei.ExceptionPointers = ep; mei.ClientPointers = FALSE; pMei = &mei; }

  const BOOL ok = MiniDumpWriteDump(process, pid, hFile, flags, pMei,
                                    const_cast<PMINIDUMP_USER_STREAM_INFORMATION>(user),
                                    const_cast<PMINIDUMP_CALLBACK_INFORMATION>(cb));
  CloseHandle(hFile);
  return ok == TRUE;
}
inline std::wstring WriteMiniDumpInDir(const std::wstring& dir,
                                       const wchar_t* appName,
                                       const wchar_t* appVersion,
                                       DumpPreset preset,
                                       EXCEPTION_POINTERS* ep = nullptr,
                                       const MINIDUMP_USER_STREAM_INFORMATION* user = nullptr,
                                       const MINIDUMP_CALLBACK_INFORMATION* cb   = nullptr) {
  std::wstring dd = dir.empty() ? DefaultDumpDirNearExe() : dir;
  if (!EnsureDirectoryTree(dd)) return L"";
  const std::wstring path = JoinPathW(dd, MakeDumpFileNameW(appName, appVersion));
  if (WriteMiniDumpToFile(path, FlagsForPreset(preset), ep, user, cb)) return path;
  return L"";
}
inline bool WriteManualDumpNow(const std::wstring& dumpDir,
                               const wchar_t* appName,
                               const wchar_t* appVersion = L"") {
  return WriteProcessMiniDump(dumpDir, appName, appVersion, nullptr);
}

// ---------- System / Thread / Module summaries -----------------------------------------------------
inline std::wstring BuildSystemInfoText() {
  std::wstring out;
  // OS version (GetVersionEx is deprecated; we keep it minimal)
  OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(push)
#pragma warning(disable:4996)
  if (GetVersionExW(&vi)) {
    wchar_t b[128]; swprintf(b, 128, L"OS=%u.%u (build=%u)\r\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    out += b;
  }
#pragma warning(pop)
  SYSTEM_INFO si{}; GetSystemInfo(&si);
  wchar_t b2[128]; swprintf(b2, 128, L"CPU=%u  PageSize=%u  ProcMask=0x%08lx\r\n",
                            (unsigned)si.dwNumberOfProcessors, (unsigned)si.dwPageSize, (unsigned long)si.dwActiveProcessorMask);
  out += b2;
  wchar_t b3[64]; swprintf(b3, 64, L"UptimeSec=%llu\r\n", (unsigned long long)(GetTickCount64()/1000));
  out += b3;
  return out;
}
inline std::wstring BuildThreadSummaryText() {
  std::wstring out = L"[Threads]\r\n"; const DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  THREADENTRY32 te{}; te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do if (te.th32OwnerProcessID == pid) {
      wchar_t line[128];
      swprintf(line, 128, L"TID=%lu  BasePri=%ld  DeltaPri=%ld\r\n",
               (unsigned long)te.th32ThreadID, (long)te.tpBasePri, (long)te.tpDeltaPri);
      out += line;
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap); return out;
}
inline std::wstring BuildModuleSummaryText() {
  std::wstring out = L"[Modules]\r\n"; const DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snap == INVALID_HANDLE_VALUE) return out;
  MODULEENTRY32W me{}; me.dwSize = sizeof(me);
  if (Module32FirstW(snap, &me)) {
    do {
      wchar_t line[MAX_PATH + 64];
      swprintf(line, _countof(line), L"%s  Base=0x%p  Size=%lu\r\n", me.szModule, me.modBaseAddr, (unsigned long)me.modBaseSize);
      out += line;
    } while (Module32NextW(snap, &me));
  }
  CloseHandle(snap); return out;
}

// ---------- Crash keys (global + scoped RAII) -----------------------------------------------------
namespace detail { inline std::mutex& CrashKeyMu(){ static std::mutex m; return m; }
                  inline std::map<std::wstring,std::wstring>& CrashKeys(){ static std::map<std::wstring,std::wstring> kv; return kv; } }
inline void SetCrashKey(std::wstring key, std::wstring value) { std::lock_guard<std::mutex> lk(detail::CrashKeyMu()); detail::CrashKeys()[std::move(key)] = std::move(value); }
inline void ClearCrashKey(const std::wstring& key) { std::lock_guard<std::mutex> lk(detail::CrashKeyMu()); detail::CrashKeys().erase(key); }
inline std::wstring BuildCrashKeysJsonW() {
  std::lock_guard<std::mutex> lk(detail::CrashKeyMu());
  std::wstring j = L"{"; bool first = true;
  for (auto& kv : detail::CrashKeys()) { if (!first) j += L','; first = false; j += L"\"" + kv.first + L"\":\"" + kv.second + L"\""; }
  j += L"}"; return j;
}
class ScopedCrashKey {
public: ScopedCrashKey(std::wstring k, std::wstring v) : k_(std::move(k)) { SetCrashKey(k_, std::move(v)); }
        ~ScopedCrashKey(){ ClearCrashKey(k_); }
        ScopedCrashKey(const ScopedCrashKey&) = delete; ScopedCrashKey& operator=(const ScopedCrashKey&) = delete;
private: std::wstring k_;
};

// ---------- Annotated unhandled filter override (optional) ----------------------------------------
// SetUnhandledExceptionFilter docs: :contentReference[oaicite:5]{index=5}
namespace detail {
  struct AnnotCfg { std::wstring dir, app, ver; DumpPreset preset = DumpPreset::Medium;
                    std::atomic<long> singleShot{0}; bool include_threads=true, include_system=true, include_keys=true, include_modules=true; };
  inline AnnotCfg& ACfg() { static AnnotCfg c; return c; }
  inline LONG WINAPI AnnotUnhandledFilter(EXCEPTION_POINTERS* ep) {
    auto& c = ACfg();
    if (c.singleShot.fetch_add(1) == 0) {
      UserStreamBuilder usb;
      if (c.include_system)  usb.AddTextW(BuildSystemInfoText(), CommentStreamW);
      if (c.include_threads) usb.AddTextW(BuildThreadSummaryText(), CommentStreamW);
      if (c.include_modules) usb.AddTextW(BuildModuleSummaryText(), CommentStreamW);
      if (c.include_keys)    usb.AddTextW(BuildCrashKeysJsonW(),   CommentStreamW);
      auto us = usb.Build();
      (void)WriteMiniDumpInDir(c.dir, c.app.c_str(), c.ver.c_str(), c.preset, ep, &us, nullptr);
    }
    return EXCEPTION_EXECUTE_HANDLER;
  }
}
inline void OverrideUnhandledFilterWithAnnotations(const std::wstring& dumpDir,
                                                   const wchar_t* appName,
                                                   const wchar_t* appVersion = L"",
                                                   DumpPreset preset = DumpPreset::Medium,
                                                   bool includeCrashKeys = true,
                                                   bool includeThreadSummary = true,
                                                   bool includeSystemInfo = true,
                                                   bool includeModules = true,
                                                   bool singleShot = true) {
  auto& c = detail::ACfg();
  c.dir = dumpDir; c.app = appName?appName:L"App"; c.ver = appVersion?appVersion:L"";
  c.preset = preset; c.include_keys = includeCrashKeys; c.include_threads = includeThreadSummary; c.include_system = includeSystemInfo; c.include_modules = includeModules;
  c.singleShot.store(singleShot ? 0 : -1);
  SetUnhandledExceptionFilter(&detail::AnnotUnhandledFilter);
}

// ---------- Process Error Mode RAII ---------------------------------------------------------------
class ScopedProcessErrorMode {
public:
  explicit ScopedProcessErrorMode(bool hideDialogs) {
    if (hideDialogs) { prev_ = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX); applied_ = true; }
  }
  ~ScopedProcessErrorMode(){ if (applied_) SetErrorMode(prev_); }
  ScopedProcessErrorMode(const ScopedProcessErrorMode&) = delete; ScopedProcessErrorMode& operator=(const ScopedProcessErrorMode&) = delete;
private: UINT prev_ = 0; bool applied_ = false;
};

// ---------- Console CTRL handler / CRT hooks ------------------------------------------------------
// Console handler docs: :contentReference[oaicite:6]{index=6}
namespace detail {
  inline BOOL WINAPI CtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
      case CTRL_CLOSE_EVENT:
        (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"console"); return TRUE;
      default: return FALSE;
    }
  }
  inline void __cdecl InvalidParamHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"invalidparam");
    RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0, nullptr);
  }
  inline void __cdecl PurecallHandler() {
    (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"purecall");
    RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0, nullptr);
  }
  inline int  __cdecl NewHandler(std::size_t) {
    (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"oom"); return 0;
  }
  inline void __cdecl TerminateHandler() {
    (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"terminate"); abort();
  }
  inline void __cdecl AbortSignalHandler(int){ (void)WriteProcessMiniDump(DefaultDumpDirNearExe(), L"ColonyGame", L"abort"); }
}
struct InstallOptions {
  std::wstring dump_directory; std::wstring app_name; std::wstring app_version;
  bool hide_wer_dialog=true, add_vectored_handler=true, hook_invalid_param=true, hook_purecall=true, hook_new_handler=true, hook_terminate=true, hook_abort_signal=true, hook_console_ctrl=true;
  size_t keep_newest=20; uint64_t cap_total_bytes=0;
};
inline void InstallCrashDumpHandlerEx(const InstallOptions& opt) {
  auto dir = opt.dump_directory.empty() ? DefaultDumpDirLocalAppData(L"Colony-Game", L"ColonyGame") : opt.dump_directory;
  EnsureDirectoryTree(dir);
  InstallCrashDumpHandler(dir, opt.app_name.c_str(), opt.app_version.c_str()); // core handler in .cpp (top-level filter)
  ScopedProcessErrorMode em(opt.hide_wer_dialog); (void)em;

  if (opt.add_vectored_handler)  (void)AddVectoredExceptionHandler(1, nullptr); // placeholder; app may add its own detailed VEH later
  if (opt.hook_invalid_param)    _set_invalid_parameter_handler(&detail::InvalidParamHandler);
  if (opt.hook_purecall)         _set_purecall_handler(&detail::PurecallHandler);
  if (opt.hook_new_handler)      std::set_new_handler(&detail::NewHandler);
  if (opt.hook_terminate)        std::set_terminate(&detail::TerminateHandler);
  if (opt.hook_abort_signal)     signal(SIGABRT, &detail::AbortSignalHandler);
  if (opt.hook_console_ctrl)     SetConsoleCtrlHandler(&detail::CtrlHandler, TRUE);

  if (opt.keep_newest)           PruneByNewestN(dir, opt.keep_newest);
  if (opt.cap_total_bytes)       PruneByMaxBytes(dir, opt.cap_total_bytes);
}

// ---------- Event Log reporting (Application log) -------------------------------------------------
// ReportEventW / RegisterEventSourceW: :contentReference[oaicite:7]{index=7}
inline void ReportEventLogCrash(const std::wstring& sourceName, WORD type /*EVENTLOG_*_TYPE*/, const std::wstring& msg) {
  HANDLE src = RegisterEventSourceW(nullptr, sourceName.c_str()); if (!src) return;
  LPCWSTR arr[1] = { msg.c_str() };
  ReportEventW(src, type, 0, 0xC0000001 /*custom*/, nullptr, 1, 0, arr, nullptr);
  DeregisterEventSource(src);
}

// ---------- NTFS compression (reduce disk footprint) ----------------------------------------------
// FSCTL_SET_COMPRESSION / DeviceIoControl docs: :contentReference[oaicite:8]{index=8}
inline bool TrySetNtfsCompression(const std::wstring& path, bool isDirectory, USHORT state /*0=NONE,1=DEFAULT,2=LZNT1*/) {
  DWORD access = GENERIC_READ | GENERIC_WRITE;
  DWORD share  = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD flags  = isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
  HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, flags, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD bytes = 0; BOOL ok = DeviceIoControl(h, FSCTL_SET_COMPRESSION, &state, sizeof(state), nullptr, 0, &bytes, nullptr);
  CloseHandle(h); return ok == TRUE;
}

// ---------- Symbol path helper --------------------------------------------------------------------
inline void SetDefaultMsftSymbolPath(const std::wstring& localCache = L"C:\\Symbols") {
  std::wstring v = L"srv*" + localCache + L"*https://msdl.microsoft.com/download/symbols";
  SetEnvironmentVariableW(L"_NT_SYMBOL_PATH", v.c_str());
}

// ---------- Breadcrumbs (append-only text file) ---------------------------------------------------
class Breadcrumbs {
public:
  explicit Breadcrumbs(std::wstring path) : path_(std::move(path)) {}
  void AppendLine(const std::wstring& line) {
    std::lock_guard<std::mutex> lock(mu_);
    HANDLE h = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER zero{}; SetFilePointerEx(h, zero, nullptr, FILE_END);
    std::wstring out = line; if (out.empty() || out.back() != L'\n') out += L"\r\n";
    DWORD bytes = 0; (void)WriteFile(h, out.data(), (DWORD)(out.size() * sizeof(wchar_t)), &bytes, nullptr);
    CloseHandle(h);
  }
  const std::wstring& Path() const noexcept { return path_; }
private: std::wstring path_; std::mutex mu_;
};

// ---------- Write dump + breadcrumbs + summaries ---------------------------------------------------
inline std::wstring WriteDumpWithBreadcrumbs(const std::wstring& dumpDir,
                                             const wchar_t* appName,
                                             const wchar_t* appVersion,
                                             const std::wstring& breadcrumbFile,
                                             DumpPreset preset = DumpPreset::Medium,
                                             EXCEPTION_POINTERS* ep = nullptr) {
  UserStreamBuilder usb;
  usb.AddTextW(L"[Breadcrumbs]\r\n"); usb.AddTextFileA(breadcrumbFile);
  usb.AddTextW(L"\r\n[System]\r\n" + BuildSystemInfoText());
  usb.AddTextW(L"\r\n" + BuildThreadSummaryText());
  usb.AddTextW(L"\r\n" + BuildModuleSummaryText());
  auto info = usb.Build();
  return WriteMiniDumpInDir(dumpDir, appName, appVersion, preset, ep, &info, nullptr);
}

// ---------- Sidecar triage (.txt) writer -----------------------------------------------------------
inline std::wstring ExceptionCodeToName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return L"ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return L"ARRAY_BOUNDS";
    case EXCEPTION_BREAKPOINT: return L"BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return L"DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return L"FLT_DIV_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return L"ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return L"INT_DIV_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return L"STACK_OVERFLOW";
    default: return L"UNKNOWN";
  }
}
inline void WriteSidecarTriageText(const std::wstring& dumpPath, EXCEPTION_POINTERS* ep, const std::wstring& extra = L"") {
  std::wstring txt = dumpPath; size_t p = txt.rfind(L'.'); if (p != std::wstring::npos) txt.replace(p, txt.size()-p, L".txt"); else txt += L".txt";
  FILE* f = _wfopen(txt.c_str(), L"wb"); if (!f) return;
  std::wstring content = L"Colony-Game Crash Triage\r\nDump=" + dumpPath + L"\r\n";
  if (ep && ep->ExceptionRecord) {
    wchar_t line[256];
    swprintf(line, 256, L"Exception=0x%08lX (%s)\r\nAddress=0x%p\r\n",
             ep->ExceptionRecord->ExceptionCode, ExceptionCodeToName(ep->ExceptionRecord->ExceptionCode).c_str(),
             ep->ExceptionRecord->ExceptionAddress);
    content += line;
  }
  content += BuildSystemInfoText(); content += BuildThreadSummaryText();
  if (!extra.empty()) content += L"\r\n" + extra + L"\r\n";
  fwrite(content.c_str(), sizeof(wchar_t), content.size(), f); fclose(f);
}

// ---------- Rate limiter (simple window: max N dumps per M seconds) -------------------------------
class DumpRateLimiter {
public:
  DumpRateLimiter(size_t max_dumps, DWORD window_seconds)
    : max_(max_dumps), window_ms_(window_seconds*1000ULL) {}
  bool Allow() {
    const ULONGLONG now = GetTickCount64();
    // clear old timestamps
    while (!q_.empty() && (now - q_.front()) > window_ms_) q_.erase(q_.begin());
    if (q_.size() >= max_) return false;
    q_.push_back(now); return true;
  }
private:
  size_t max_; ULONGLONG window_ms_; std::vector<ULONGLONG> q_;
};

// ---------- WER LocalDumps (admin-only) -----------------------------------------------------------
// Registry contract for LocalDumps (DumpType/Folder/Count/CustomDumpFlags): :contentReference[oaicite:9]{index=9}
inline bool TryEnableWerLocalDumps(const std::wstring& appExeName,
                                   const std::wstring& dumpFolderExpandSz,
                                   DWORD dumpType /*0=Custom,1=Mini,2=Full*/,
                                   DWORD dumpCount /*max kept*/,
                                   DWORD customFlags /*MINIDUMP_TYPE if dumpType==0*/) {
  HKEY hk = nullptr;
  LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr);
  if (r != ERROR_SUCCESS) return false;
  RegCloseKey(hk);

  std::wstring sub = L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\"; sub += appExeName;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS) return false;

  (void)RegSetValueExW(hk, L"DumpType",  0, REG_DWORD, reinterpret_cast<const BYTE*>(&dumpType), sizeof(DWORD));
  (void)RegSetValueExW(hk, L"DumpCount", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dumpCount), sizeof(DWORD));
  if (dumpType == 0) (void)RegSetValueExW(hk, L"CustomDumpFlags", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&customFlags), sizeof(DWORD));
  (void)RegSetValueExW(hk, L"DumpFolder", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(dumpFolderExpandSz.c_str()),
                       (DWORD)((dumpFolderExpandSz.size()+1) * sizeof(wchar_t)));
  RegCloseKey(hk); return true;
}
inline bool IsWerLocalDumpsConfigured() {
  HKEY hk = nullptr; LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
    L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps", 0, KEY_READ, &hk);
  if (r == ERROR_SUCCESS) { RegCloseKey(hk); return true; } return false;
}

// ---------- Convenience: annotated dump now -------------------------------------------------------
inline std::wstring WriteAnnotatedDumpNow(const std::wstring& dir,
                                          const wchar_t* app,
                                          const wchar_t* ver,
                                          DumpPreset preset,
                                          std::initializer_list<std::pair<std::string,std::string>> kvPairs,
                                          EXCEPTION_POINTERS* ep = nullptr) {
  UserStreamBuilder usb;
#if defined(CG_VERSION) || defined(CG_TOOLCHAIN) || defined(CG_BUILD_TYPE)
  usb.AddKeyValuesJson({
    {"version",  std::string((CG_VERSION[0]? CG_VERSION:""))},
    {"toolchain",std::string((CG_TOOLCHAIN[0]? CG_TOOLCHAIN:""))},
    {"config",   std::string((CG_BUILD_TYPE[0]? CG_BUILD_TYPE:""))}
  });
#endif
  usb.AddKeyValuesJson(kvPairs);
  auto info = usb.Build();
  return WriteMiniDumpInDir(dir, app, ver, preset, ep, &info, nullptr);
}

// ---------- Guard macros (SEH -> dump -> continue) ------------------------------------------------
#define CRASH_GUARD_DO(_dir, _app, _ver, _code)                                                \
  __try { _code } __except( (crash::WriteProcessMiniDump((_dir), (_app), (_ver),               \
                        GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) ) { }
#define CRASH_GUARD_BLOCK(_dir, _app, _ver)  __try {
#define CRASH_GUARD_BLOCK_END(_dir, _app, _ver)                                                \
  } __except( (crash::WriteProcessMiniDump((_dir), (_app), (_ver),                             \
                        GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) ) { }

// ---------- Test trigger --------------------------------------------------------------------------
inline void TriggerTestCrash() {
#if defined(_MSC_VER)
#  if defined(__has_builtin)
#    if __has_builtin(__fastfail)
       __fastfail(0xDE);
#    else
       RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0, nullptr);
#    endif
#  else
     __fastfail(0xDE);
#  endif
#else
  RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0, nullptr);
#endif
}

} // namespace crash
