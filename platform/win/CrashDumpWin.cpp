// CrashDumpWin.cpp
//
// Robust Windows minidump facility with dump levels, retention, throttling,
// CRT/signal integration, user streams, breadcrumbs, crash keys, optional WER helpers,
// and dynamic loading of dbghelp/wer. Public domain / CC0-style.
//
// Compatible with a minimal CrashDumpWin.h declaring:
//   namespace CrashDumpWin {
//     bool Init(const wchar_t* appName=nullptr,
//               const wchar_t* dumpDir=nullptr,
//               const wchar_t* buildTag=nullptr);
//     bool WriteManualDump(const wchar_t* reason=L"Manual");
//     void SetDumpType(MINIDUMP_TYPE type);
//     void Shutdown();
//   }
//
// ---------------------- EXTRA PUBLIC API (OPTIONAL) ----------------------
// Add these to CrashDumpWin.h if you want to call them from other TUs:
//
//   // Levels: 0=Tiny, 1=Small, 2=Balanced (default), 3=Heavy, 4=Full
//   void SetDumpLevel(int level);
//
//   // Post-crash behavior: 0=Return, 1=ExitProcess (default), 2=TerminateProcess
//   void SetPostCrashAction(int action);
//
//   // Keep at most N dumps in the folder (default 10)
//   void SetMaxDumpsToKeep(DWORD n);
//
//   // Collapse multiple crashes within N seconds (default 3s)
//   void SetThrottleSeconds(DWORD seconds);
//
//   // Skip writing a dump if a debugger is attached (default true)
//   void SetSkipIfDebuggerPresent(bool skip);
//
//   // Extra line appended to the comment stream
//   void SetExtraCommentLine(const wchar_t* line);
//
//   // Add/Remove key->value pairs shown in the comment stream
//   void SetCrashKey(const wchar_t* key, const wchar_t* value);
//   void RemoveCrashKey(const wchar_t* key);
//   void ClearCrashKeys();
//
//   // Breadcrumbs (ring buffer, default capacity 64)
//   void AddBreadcrumb(const wchar_t* fmt, ...);
//   void SetBreadcrumbCapacity(unsigned cap);
//
//   // Provide a UTF-8 "log tail" to embed as a custom user stream
//   typedef size_t (*LogTailCallback)(void* user, char* dst, size_t capBytes);
//   void SetLogTailCallback(LogTailCallback cb, void* user, size_t maxBytes);
//
//   // Optional sidecar .txt metadata
//   void EnableSidecarMetadata(bool enable);
//
//   // Optional pre/post-dump callbacks (e.g., flush logs / notify UI)
//   void SetPreDumpCallback(void (*fn)());
//   void SetPostDumpCallback(void (*fn)(const wchar_t* path, bool ok));
//
//   // Configure WER LocalDumps (HKCU) for your exe as a fallback
//   bool ConfigureWERLocalDumps(const wchar_t* exeName,
//                               const wchar_t* dumpFolder,
//                               DWORD dumpType /*1=minidump, 2=full*/,
//                               DWORD dumpCount /*e.g., 10*/);
//
//   // For testing: deliberately crash
//   void SimulateCrash();
//
// ------------------------------------------------------------------------

#if !defined(_WIN32)

// Non-Windows stub (keeps builds happy on other platforms)
#include "CrashDumpWin.h"
namespace CrashDumpWin {
bool Init(const wchar_t*, const wchar_t*, const wchar_t*) { return false; }
bool WriteManualDump(const wchar_t*) { return false; }
void SetDumpType(int) {}
void Shutdown() {}
// Optional no-op stubs can be added if needed.
}
#else

#include "CrashDumpWin.h"

#ifndef UNICODE
  #define UNICODE
#endif
#ifndef _UNICODE
  #define _UNICODE
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dbghelp.h>     // types; function is loaded dynamically
#include <winternl.h>    // RtlGetVersion (queried dynamically)
#include <psapi.h>       // PROCESS_MEMORY_COUNTERS structs (we'll load function dynamically)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <csignal>
#include <new>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>

#pragma warning(push)
#pragma warning(disable: 4996) // _snwprintf, GetVersionExW (fallback)

// ------------------ WER minimal interop (dynamic) -------------------
#ifndef WER_FAULT_REPORTING_FLAG_NOHEAP
  #define WER_FAULT_REPORTING_FLAG_NOHEAP 0x00000001
#endif
#ifndef WER_FAULT_REPORTING_FLAG_QUEUE
  #define WER_FAULT_REPORTING_FLAG_QUEUE  0x00000004
#endif
typedef HRESULT (WINAPI* WerSetFlags_t)(DWORD);

// ----------------------------- Helpers ------------------------------
namespace {

using MiniDumpWriteDump_t = BOOL (WINAPI*)(
    HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    const PMINIDUMP_EXCEPTION_INFORMATION,
    const PMINIDUMP_USER_STREAM_INFORMATION,
    const PMINIDUMP_CALLBACK_INFORMATION);

// *** PATCH B (enum mismatch fix): unify on the public type ***
using DumpLevel = CrashDumpWin::DumpLevel;

enum class PostCrash { Return=0, ExitProcess=1, TerminateProcess=2 };

struct Strings {
    static std::wstring From(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }
    static std::wstring Trim(std::wstring s) {
        auto ws = [](wchar_t c){ return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'; };
        while (!s.empty() && ws(s.front())) s.erase(s.begin());
        while (!s.empty() && ws(s.back()))  s.pop_back();
        return s;
    }
};

struct Path {
    static std::wstring ExePath() {
        wchar_t p[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, p, MAX_PATH);
        return p;
    }
    static std::wstring ExeDir() {
        auto p = ExePath();
        auto pos = p.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
    }
    static std::wstring ExeBaseNoExt() {
        auto p = ExePath();
        auto pos = p.find_last_of(L"\\/");
        std::wstring n = (pos == std::wstring::npos) ? p : p.substr(pos+1);
        auto dot = n.find_last_of(L'.');
        if (dot != std::wstring::npos) n.resize(dot);
        return n;
    }
    static bool EnsureDirRecursive(const std::wstring& in) {
        if (in.empty()) return false;
        std::wstring p = in; for (auto& c : p) if (c == L'/') c = L'\\';
        // Create progressively
        size_t start = 0;
        if (p.rfind(L"\\\\?\\", 0) == 0) start = 4;
        for (size_t i = start; i < p.size(); ++i) {
            if (p[i] == L'\\') {
                std::wstring partial = p.substr(0, i);
                if (!partial.empty()) CreateDirectoryW(partial.c_str(), nullptr);
            }
        }
        if (CreateDirectoryW(p.c_str(), nullptr)) return true;
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }
};

static inline ULONGLONG NowTick() { return GetTickCount64(); }

static std::wstring TimeStampUTC() {
    SYSTEMTIME st; GetSystemTime(&st);
    wchar_t t[64];
    _snwprintf(t, _countof(t), L"%04u%02u%02u_%02u%02u%02u_%03u",
               (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
               (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
               (unsigned)st.wMilliseconds);
    return t;
}

static std::wstring OSVersionString() {
    typedef LONG (WINAPI* RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto p = reinterpret_cast<RtlGetVersion_t>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (p) {
            RTL_OSVERSIONINFOW v = {}; v.dwOSVersionInfoSize = sizeof(v);
            if (p(&v) == 0) {
                wchar_t s[128];
                _snwprintf(s, _countof(s), L"%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
                return s;
            }
        }
    }
    OSVERSIONINFOW vi = {}; vi.dwOSVersionInfoSize = sizeof(vi);
    if (GetVersionExW(&vi)) {
        wchar_t s[128];
        _snwprintf(s, _countof(s), L"%lu.%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
        return s;
    }
    return L"unknown";
}

static std::wstring ArchString() {
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        default: return L"other";
    }
}

static const wchar_t* ExcName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return L"ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return L"ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return L"BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return L"DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return L"FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return L"FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return L"FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return L"FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return L"FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return L"FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return L"FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return L"ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return L"IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return L"INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return L"INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return L"INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return L"NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return L"PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return L"SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return L"STACK_OVERFLOW";
        default:                                 return L"UNKNOWN";
    }
}

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s; s.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ------------------------ Global state & config ----------------------
struct Globals {
    // Identity & output
    std::wstring appName;
    std::wstring dumpDir;
    std::wstring buildTag;

    // Behavior
    MINIDUMP_TYPE dumpType;
    DumpLevel     level;
    PostCrash     postAction;
    bool          skipIfDebuggerPresent;
    bool          writeSidecar;
    DWORD         throttleSeconds;
    DWORD         maxDumpsToKeep;
    bool          suppressDialogs;

    // Callbacks
    void (*preDumpCb)() = nullptr;
    void (*postDumpCb)(const wchar_t* path, bool ok) = nullptr;

    // dbghelp & WER
    HMODULE hDbgHelp = nullptr;
    MiniDumpWriteDump_t pMiniDumpWriteDump = nullptr;
    HMODULE hWer = nullptr;
    WerSetFlags_t pWerSetFlags = nullptr;

    // Retention/throttle
    std::atomic<ULONGLONG> lastDumpTick{0};
    std::atomic<LONG> inHandler{0};

    // CRT/SEH
    LPTOP_LEVEL_EXCEPTION_FILTER prevUnhandled = nullptr;
    PVOID vehHandle = nullptr;

    // Uptime baseline
    ULONGLONG startTick = GetTickCount64();

    // Extra comment & keys
    std::wstring extraComment;
    struct KV { std::wstring k, v; };
    std::vector<KV> keys;
    SRWLOCK keysLock = SRWLOCK_INIT;

    // Breadcrumbs
    std::vector<std::wstring> crumbs;
    unsigned crumbCap = 64;
    SRWLOCK crumbLock = SRWLOCK_INIT;
    std::atomic<uint32_t> crumbSeq{0};

    // Log tail (UTF-8)
    using LogTailCallback = size_t (*)(void*, char*, size_t);
    LogTailCallback logCb = nullptr;
    void* logUser = nullptr;
    size_t logMaxBytes = 0;
};
static Globals G;

// SRW lock RAII
struct SRWExclusive {
    SRWLOCK* L; explicit SRWExclusive(SRWLOCK* p) : L(p) { AcquireSRWLockExclusive(L); }
    ~SRWExclusive(){ ReleaseSRWLockExclusive(L); }
};

static MINIDUMP_TYPE PresetFor(DumpLevel lvl) {
    switch (lvl) {
        case DumpLevel::Tiny:
            return (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        case DumpLevel::Small:
            return (MINIDUMP_TYPE)(
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory);
        case DumpLevel::Balanced:
            return (MINIDUMP_TYPE)(
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory);
        case DumpLevel::Heavy:
            return (MINIDUMP_TYPE)(
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithPrivateReadWriteMemory |
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory);
        case DumpLevel::Full:
        default:
            return (MINIDUMP_TYPE)(
                MiniDumpWithFullMemory |
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithDataSegs |
                MiniDumpWithCodeSegs |
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory);
    }
}

static void DebugOut(const wchar_t* fmt, ...) {
    wchar_t b[1024];
    va_list vl; va_start(vl, fmt);
    _vsnwprintf(b, _countof(b), fmt, vl);
    va_end(vl);
    OutputDebugStringW(b);
}

static bool LoadDbgHelp() {
    if (G.pMiniDumpWriteDump) return true;
    std::wstring local = Path::ExeDir() + L"\\dbghelp.dll";
    G.hDbgHelp = LoadLibraryW(local.c_str());
    if (!G.hDbgHelp) G.hDbgHelp = LoadLibraryW(L"dbghelp.dll");
    if (!G.hDbgHelp) return false;
    G.pMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress(G.hDbgHelp, "MiniDumpWriteDump");
    if (!G.pMiniDumpWriteDump) {
        FreeLibrary(G.hDbgHelp);
        G.hDbgHelp = nullptr;
        return false;
    }
    return true;
}

static void LoadWer() {
    if (G.pWerSetFlags) return;
    G.hWer = LoadLibraryW(L"wer.dll");
    if (!G.hWer) return;
    G.pWerSetFlags = (WerSetFlags_t)GetProcAddress(G.hWer, "WerSetFlags");
}

static DWORD ReadEnvDword(const wchar_t* key, DWORD fallback) {
    wchar_t buf[32]; DWORD n = GetEnvironmentVariableW(key, buf, _countof(buf));
    if (!n || n >= _countof(buf)) return fallback;
    return (DWORD)_wtoi(buf);
}
static bool ReadEnvBool(const wchar_t* key, bool fallback) {
    wchar_t buf[16]; DWORD n = GetEnvironmentVariableW(key, buf, _countof(buf));
    if (!n || n >= _countof(buf)) return fallback;
    if (_wcsicmp(buf, L"true")==0 || _wcsicmp(buf, L"yes")==0) return true;
    if (_wcsicmp(buf, L"false")==0 || _wcsicmp(buf, L"no")==0) return false;
    return _wtoi(buf) != 0;
}

static void ApplyEnv() {
    // Folder
    wchar_t dir[1024]; DWORD n = GetEnvironmentVariableW(L"CRASHDUMP_DIR", dir, _countof(dir));
    if (n && n < _countof(dir)) G.dumpDir = dir;

    G.maxDumpsToKeep      = ReadEnvDword(L"CRASHDUMP_MAX",           G.maxDumpsToKeep);
    G.throttleSeconds     = ReadEnvDword(L"CRASHDUMP_THROTTLE_SEC",  G.throttleSeconds);
    G.skipIfDebuggerPresent = ReadEnvBool(L"CRASHDUMP_SKIP_DEBUGGER", G.skipIfDebuggerPresent);

    if (ReadEnvBool(L"CRASHDUMP_FULLMEM", false)) {
        G.dumpType = (MINIDUMP_TYPE)(G.dumpType | MiniDumpWithFullMemory | MiniDumpWithPrivateReadWriteMemory);
    }
    wchar_t post[16]; n = GetEnvironmentVariableW(L"CRASHDUMP_POST", post, _countof(post));
    if (n && n < _countof(post)) {
        if (_wcsicmp(post, L"return")==0)     G.postAction = PostCrash::Return;
        else if (_wcsicmp(post, L"terminate")==0) G.postAction = PostCrash::TerminateProcess;
        else G.postAction = PostCrash::ExitProcess;
    }
}

static std::wstring DumpDir() {
    if (!G.dumpDir.empty()) return G.dumpDir;
    return Path::ExeDir() + L"\\Dumps";
}

static std::wstring ComposeDumpPath(const wchar_t* reason) {
    auto dir = DumpDir();
    Path::EnsureDirRecursive(dir);
    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();
    std::wstring app = G.appName.empty() ? Path::ExeBaseNoExt() : G.appName;
    std::wstring ts  = TimeStampUTC();

    wchar_t fn[768] = {};
    _snwprintf(fn, _countof(fn), L"%s_%s_pid%lu_tid%lu%s%s.dmp",
               app.c_str(), ts.c_str(), (unsigned long)pid, (unsigned long)tid,
               (reason && *reason) ? L"_" : L"", (reason && *reason) ? reason : L"");
    return dir + L"\\" + fn;
}

static void DeleteOldDumpsIfNeeded() {
    if (G.maxDumpsToKeep == 0) return;
    std::wstring dir = DumpDir();
    std::wstring mask = dir + L"\\*.dmp";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    struct E { std::wstring p; FILETIME t; };
    std::vector<E> v;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            v.push_back({ dir + L"\\" + fd.cFileName, fd.ftLastWriteTime });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (v.size() <= G.maxDumpsToKeep) return;
    std::sort(v.begin(), v.end(), [](const E& a, const E& b){
        ULONGLONG ta = (ULONGLONG)a.t.dwHighDateTime<<32 | a.t.dwLowDateTime;
        ULONGLONG tb = (ULONGLONG)b.t.dwHighDateTime<<32 | b.t.dwLowDateTime;
        return ta < tb; // oldest first
    });
    size_t kill = v.size() - (size_t)G.maxDumpsToKeep;
    for (size_t i=0; i<kill; ++i) DeleteFileW(v[i].p.c_str());
}

static bool ShouldThrottle() {
    if (G.throttleSeconds == 0) return false;
    ULONGLONG now = NowTick();
    ULONGLONG last = G.lastDumpTick.load(std::memory_order_relaxed);
    if (last && (now - last) < (ULONGLONG)G.throttleSeconds*1000ULL) return true;
    G.lastDumpTick.store(now, std::memory_order_relaxed);
    return false;
}

// process memory info (best-effort, dynamic)
static std::wstring ProcessMemorySummary() {
    typedef BOOL (WINAPI *GetProcessMemoryInfo_t)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    HMODULE h = LoadLibraryW(L"psapi.dll");
    if (!h) return L"(mem: n/a)";
    auto f = reinterpret_cast<GetProcessMemoryInfo_t>(GetProcAddress(h, "GetProcessMemoryInfo"));
    if (!f) { FreeLibrary(h); return L"(mem: n/a)"; }

    PROCESS_MEMORY_COUNTERS pm = {};
    std::wstring s = L"(mem: n/a)";
    if (f(GetCurrentProcess(), &pm, sizeof(pm))) {
        wchar_t b[256];
        _snwprintf(b, _countof(b), L"WorkingSet=%lu KB, PeakWorkingSet=%lu KB, Pagefile=%lu KB",
                   (unsigned long)(pm.WorkingSetSize/1024),
                   (unsigned long)(pm.PeakWorkingSetSize/1024),
                   (unsigned long)(pm.PagefileUsage/1024));
        s = b;
    }
    FreeLibrary(h);
    return s;
}

// -------------------- User streams & callbacks -----------------------
static const MINIDUMP_STREAM_TYPE kStreamUtf8LogTail =
    (MINIDUMP_STREAM_TYPE)((int)LastReservedStream + 1);

struct UserStreams {
    std::wstring commentW;              // alive during dump
    std::string  logTail;               // alive during dump
    MINIDUMP_USER_STREAM streams[2]{};  // comment + optional log
    MINIDUMP_USER_STREAM_INFORMATION info{};
};

static std::wstring BuildComment(EXCEPTION_POINTERS* ep, const wchar_t* reason) {
    bool dbg = (IsDebuggerPresent() != 0);
    ULONGLONG uptime = NowTick() - G.startTick;

    std::wstring c;
    c.reserve(1024);
    c += L"App: "; c += (G.appName.empty() ? Path::ExeBaseNoExt() : G.appName); c += L"\n";
    c += L"Build: "; c += (G.buildTag.empty() ? L"(n/a)" : G.buildTag); c += L"\n";
    c += L"Time(UTC): "; c += TimeStampUTC(); c += L"\n";
    c += L"Exe: "; c += Path::ExePath(); c += L"\n";
    c += L"PID/TID: "; { wchar_t b[64]; _snwprintf(b, _countof(b), L"%lu/%lu", GetCurrentProcessId(), GetCurrentThreadId()); c += b; } c += L"\n";
    c += L"Uptime(ms): "; { wchar_t b[64]; _snwprintf(b, _countof(b), L"%llu", (unsigned long long)uptime); c += b; } c += L"\n";
    c += L"OS: "; c += OSVersionString(); c += L"\n";
    c += L"Arch: "; c += ArchString(); c += L"\n";
    c += L"Mem: "; c += ProcessMemorySummary(); c += L"\n";
    c += L"DebuggerPresent: "; c += (dbg?L"Yes":L"No"); c += L"\n";
    c += L"Reason: "; c += (reason?reason:L"(none)"); c += L"\n";

    if (ep && ep->ExceptionRecord) {
        auto er = ep->ExceptionRecord;
        wchar_t b[256];
        _snwprintf(b, _countof(b), L"Exception: 0x%08lX (%s)\nFlags: 0x%08lX\nAddress: %p\n",
                   er->ExceptionCode, ExcName(er->ExceptionCode), er->ExceptionFlags, er->ExceptionAddress);
        c += b;
    }

    if (!G.extraComment.empty()) {
        c += L"\n";
        c += G.extraComment;
        c += L"\n";
    }

    // Crash keys
    {
        SRWExclusive lk(&G.keysLock);
        if (!G.keys.empty()) {
            c += L"\n-- Crash Keys --\n";
            for (const auto& kv : G.keys) {
                c += L"  "; c += kv.k; c += L": "; c += kv.v; c += L"\n";
            }
        }
    }

    // Breadcrumbs
    {
        SRWExclusive lk(&G.crumbLock);
        if (!G.crumbs.empty()) {
            c += L"\n-- Breadcrumbs (newest last) --\n";
            for (const auto& s : G.crumbs) {
                c += L"  • "; c += s; c += L"\n";
            }
        }
    }

    return c;
}

static void BuildUserStreams(EXCEPTION_POINTERS* ep, const wchar_t* reason, UserStreams& u) {
    u.commentW = BuildComment(ep, reason);
    u.streams[0].Type       = CommentStreamW;
    u.streams[0].Buffer     = (void*)u.commentW.c_str();
    u.streams[0].BufferSize = (ULONG)((u.commentW.size()+1)*sizeof(wchar_t));

    size_t count = 1;
    if (G.logCb && G.logMaxBytes > 0) {
        u.logTail.resize(G.logMaxBytes);
        size_t wrote = 0;
        __try { wrote = G.logCb(G.logUser, u.logTail.data(), u.logTail.size()); }
        __except(EXCEPTION_EXECUTE_HANDLER) { wrote = 0; }
        if (wrote > u.logTail.size()) wrote = u.logTail.size();
        u.logTail.resize(wrote);
        if (!u.logTail.empty()) {
            u.streams[1].Type       = kStreamUtf8LogTail;
            u.streams[1].Buffer     = (void*)u.logTail.data();
            u.streams[1].BufferSize = (ULONG)u.logTail.size();
            count = 2;
        }
    }

    u.info.UserStreamCount = (ULONG)count;
    u.info.UserStreamArray = u.streams;
}

static BOOL CALLBACK MiniCb(PVOID, const PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT out) {
    if (!in || !out) return TRUE;
    switch (in->CallbackType) {
        case IncludeModuleCallback:
        case IncludeThreadCallback:
        case ThreadExCallback:
        case IncludeVmRegionCallback:
            return TRUE;
        case ModuleCallback:
            // Keep code segments; prune bulky data sections
            out->ModuleWriteFlags |= ModuleWriteCodeSegs;
            if (out->ModuleWriteFlags & ModuleWriteDataSeg)
                out->ModuleWriteFlags &= ~ModuleWriteDataSeg;
            return TRUE;
        case ThreadCallback:
        case MemoryCallback:
        case CancelCallback:
        default:
            return TRUE;
    }
}

static bool WriteDumpCore(EXCEPTION_POINTERS* ep, const wchar_t* reason, std::wstring* outPath) {
    if (!LoadDbgHelp()) return false;
    if (G.skipIfDebuggerPresent && IsDebuggerPresent()) return false;
    if (ShouldThrottle()) return false;

    if (G.preDumpCb) {
        __try { G.preDumpCb(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    std::wstring path = ComposeDumpPath(reason);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DebugOut(L"[CrashDump] CreateFile failed for %s (GLE=%lu)\n", path.c_str(), GetLastError());
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
    }

    UserStreams us;
    BuildUserStreams(ep, reason, us);

    MINIDUMP_CALLBACK_INFORMATION cb = {};
    cb.CallbackRoutine = MiniCb;

    BOOL ok = G.pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                                   G.dumpType, mei.ExceptionPointers ? &mei : nullptr,
                                   &us.info, &cb);
    CloseHandle(h);

    if (ok) {
        DeleteOldDumpsIfNeeded();
        if (outPath) *outPath = path;
        if (G.postDumpCb) {
            __try { G.postDumpCb(path.c_str(), true); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        DebugOut(L"[CrashDump] Dump written: %s\n", path.c_str());
        return true;
    } else {
        if (G.postDumpCb) {
            __try { G.postDumpCb(path.c_str(), false); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        DebugOut(L"[CrashDump] MiniDumpWriteDump failed (GLE=%lu)\n", GetLastError());
        return false;
    }
}

// ------------------------- Handlers -------------------------------
static void __cdecl OnPureCall() {
    WriteDumpCore(nullptr, L"PureVirtualCall", nullptr);
    TerminateProcess(GetCurrentProcess(), 0x4000);
}
static void __cdecl OnInvalidParam(const wchar_t* expr, const wchar_t* func,
                                   const wchar_t* file, unsigned line, uintptr_t) {
    wchar_t why[512] = L"InvalidParameter";
    if (expr||func||file) _snwprintf(why, _countof(why), L"InvalidParam:%s|%s|%s:%u",
                                     expr?expr:L"", func?func:L"", file?file:L"", line);
    WriteDumpCore(nullptr, why, nullptr);
    TerminateProcess(GetCurrentProcess(), 0x4001);
}
static void __cdecl OnTerminate() {
    WriteDumpCore(nullptr, L"std::terminate", nullptr);
    TerminateProcess(GetCurrentProcess(), 0x4002);
}
static void OnSignal(int sig) {
    const wchar_t* r = (sig==SIGABRT?L"SIGABRT": sig==SIGSEGV?L"SIGSEGV":
                        sig==SIGILL?L"SIGILL" : sig==SIGFPE?L"SIGFPE" : L"Signal");
    WriteDumpCore(nullptr, r, nullptr);
    TerminateProcess(GetCurrentProcess(), 0x4003);
}
static LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    if (G.inHandler.fetch_add(1) == 0) {
        std::wstring path;
        WriteDumpCore(ep, L"UnhandledException", &path);
        // Choose post-crash behavior
        switch (G.postAction) {
            case PostCrash::Return:           break;
            case PostCrash::ExitProcess:      ExitProcess(ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 1); break;
            case PostCrash::TerminateProcess: TerminateProcess(GetCurrentProcess(), ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 1); break;
        }
    }
    LONG ret = EXCEPTION_EXECUTE_HANDLER;
    if (G.prevUnhandled) {
        __try { ret = G.prevUnhandled(ep); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ret;
}

// --------------------------- Sidecar .txt (optional) --------------------------
static void WriteSidecarTxtIfEnabled(const std::wstring& dumpPath, EXCEPTION_POINTERS* ep, const wchar_t* reason) {
    if (!G.writeSidecar) return;
    std::wstring txt = dumpPath;
    auto dot = txt.find_last_of(L'.'); if (dot != std::wstring::npos) txt.replace(dot, txt.size()-dot, L".txt"); else txt += L".txt";

    HANDLE f = CreateFileW(txt.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;

    std::wstring w = BuildComment(ep, reason);
    std::string  u8 = ToUtf8(w);
    const unsigned char bom[3] = {0xEF,0xBB,0xBF};
    DWORD wr=0;
    WriteFile(f, bom, 3, &wr, nullptr);
    if (!u8.empty()) WriteFile(f, u8.data(), (DWORD)u8.size(), &wr, nullptr);
    CloseHandle(f);
}

} // anonymous namespace

// ================================ PUBLIC API ================================
namespace CrashDumpWin {

bool Init(const wchar_t* appName, const wchar_t* dumpDir, const wchar_t* buildTag) {
    // Defaults
    G.appName  = Strings::Trim(Strings::From(appName));
    G.dumpDir  = Strings::Trim(Strings::From(dumpDir));
    G.buildTag = Strings::Trim(Strings::From(buildTag));
    G.level    = DumpLevel::Balanced;
    G.dumpType = PresetFor(G.level);
    G.postAction = PostCrash::ExitProcess;
    G.skipIfDebuggerPresent = true;
    G.writeSidecar = true;
    G.throttleSeconds = 3;
    G.maxDumpsToKeep  = 10;
    G.suppressDialogs = true;

    ApplyEnv();
    LoadWer();
    if (G.suppressDialogs) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        if (G.pWerSetFlags) G.pWerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_FLAG_QUEUE);
    }

    // Install handlers
    G.prevUnhandled = SetUnhandledExceptionFilter(Unhandled);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParam);
    std::set_terminate(OnTerminate);
    signal(SIGABRT, OnSignal);
    signal(SIGSEGV, OnSignal);
    signal(SIGILL,  OnSignal);
    signal(SIGFPE,  OnSignal);

    // Breadcrumbs init
    {
        SRWExclusive lk(&G.crumbLock);
        G.crumbs.clear();
        G.crumbs.reserve(G.crumbCap);
    }

    // Preload dbghelp now (safer than loading in crash)
    LoadDbgHelp();

    DebugOut(L"[CrashDump] Init: dir=%s keep=%lu level=%d\n",
             DumpDir().c_str(), (unsigned long)G.maxDumpsToKeep, (int)G.level);
    return true;
}

bool WriteManualDump(const wchar_t* reason) {
    std::wstring path;
    bool ok = WriteDumpCore(nullptr, reason ? reason : L"Manual", &path);
    WriteSidecarTxtIfEnabled(path, nullptr, reason ? reason : L"Manual");
    return ok;
}

void SetDumpType(MINIDUMP_TYPE type) {
    G.dumpType = type;
}

void Shutdown() {
    if (G.prevUnhandled) { SetUnhandledExceptionFilter(G.prevUnhandled); G.prevUnhandled = nullptr; }
    if (G.vehHandle)     { RemoveVectoredExceptionHandler(G.vehHandle); G.vehHandle = nullptr; }
    if (G.hDbgHelp)      { FreeLibrary(G.hDbgHelp); G.hDbgHelp = nullptr; G.pMiniDumpWriteDump = nullptr; }
    if (G.hWer)          { FreeLibrary(G.hWer); G.hWer = nullptr; G.pWerSetFlags = nullptr; }
    _set_purecall_handler(nullptr);
    _set_invalid_parameter_handler(nullptr);
}

// ---------------------- OPTIONAL EXTENSIONS (export if desired) ---------------------

void SetDumpLevel(int level) {
    DumpLevel dl = DumpLevel::Balanced;
    switch (level) { case 0: dl=DumpLevel::Tiny; break; case 1: dl=DumpLevel::Small; break;
                     case 2: dl=DumpLevel::Balanced; break; case 3: dl=DumpLevel::Heavy; break;
                     case 4: dl=DumpLevel::Full; break; default: dl=DumpLevel::Balanced; break; }
    G.level = dl; G.dumpType = PresetFor(dl);
}
void SetPostCrashAction(int action) {
    switch (action) { case 0: G.postAction=PostCrash::Return; break;
                      case 2: G.postAction=PostCrash::TerminateProcess; break;
                      default:G.postAction=PostCrash::ExitProcess; break; }
}
void SetMaxDumpsToKeep(DWORD n)      { G.maxDumpsToKeep = std::max<DWORD>(1, std::min<DWORD>(1000, n)); }
void SetThrottleSeconds(DWORD sec)    { G.throttleSeconds = sec; }
void SetSkipIfDebuggerPresent(bool s) { G.skipIfDebuggerPresent = s; }

void SetExtraCommentLine(const wchar_t* line) {
    G.extraComment = line ? Strings::Trim(Strings::From(line)) : std::wstring();
}

void SetCrashKey(const wchar_t* key, const wchar_t* value) {
    if (!key || !*key) return;
    SRWExclusive lk(&G.keysLock);
    std::wstring K = key, V = value?value:L"";
    for (auto& kv: G.keys) {
        if (_wcsicmp(kv.k.c_str(), K.c_str())==0) { kv.v = V; return; }
    }
    G.keys.push_back({K, V});
}
void RemoveCrashKey(const wchar_t* key) {
    if (!key) return;
    SRWExclusive lk(&G.keysLock);
    G.keys.erase(std::remove_if(G.keys.begin(), G.keys.end(),
                [&](const Globals::KV& kv){ return _wcsicmp(kv.k.c_str(), key)==0; }),
                G.keys.end());
}
void ClearCrashKeys() {
    SRWExclusive lk(&G.keysLock);
    G.keys.clear();
}

void AddBreadcrumb(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list vl; va_start(vl, fmt);
    _vsnwprintf(msg, _countof(msg), fmt, vl);
    va_end(vl);

    wchar_t line[600];
    uint32_t seq = ++G.crumbSeq;
    _snwprintf(line, _countof(line), L"[%s #%u] %s", TimeStampUTC().c_str(), seq, msg);

    SRWExclusive lk(&G.crumbLock);
    if (G.crumbs.size() < G.crumbCap) {
        G.crumbs.emplace_back(line);
    } else {
        static size_t idx = 0;
        G.crumbs[idx] = line;
        idx = (idx + 1) % G.crumbCap;
    }
}
void SetBreadcrumbCapacity(unsigned cap) {
    SRWExclusive lk(&G.crumbLock);
    G.crumbCap = std::max(8u, std::min(4096u, cap));
    G.crumbs.clear();
    G.crumbs.reserve(G.crumbCap);
}

using LogTailCallback = Globals::LogTailCallback;
void SetLogTailCallback(LogTailCallback cb, void* user, size_t maxBytes) {
    G.logCb = cb; G.logUser = user; G.logMaxBytes = maxBytes;
}

void EnableSidecarMetadata(bool enable) { G.writeSidecar = enable; }
void SetPreDumpCallback(void (*fn)()) { G.preDumpCb = fn; }
void SetPostDumpCallback(void (*fn)(const wchar_t*, bool)) { G.postDumpCb = fn; }

bool ConfigureWERLocalDumps(const wchar_t* exeName,
                            const wchar_t* dumpFolder,
                            DWORD dumpType,
                            DWORD dumpCount) {
    HKEY hBase = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps",
        0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hBase, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    HKEY hApp = nullptr;
    const wchar_t* sub = (exeName && *exeName) ? exeName : L"*";
    if (RegCreateKeyExW(hBase, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hApp, nullptr) == ERROR_SUCCESS) {
        if (dumpFolder && *dumpFolder) {
            RegSetValueExW(hApp, L"DumpFolder", 0, REG_EXPAND_SZ,
                           (const BYTE*)dumpFolder, (DWORD)((wcslen(dumpFolder)+1)*sizeof(wchar_t)));
        }
        RegSetValueExW(hApp, L"DumpType", 0, REG_DWORD, (const BYTE*)&dumpType, sizeof(DWORD));
        RegSetValueExW(hApp, L"DumpCount",0, REG_DWORD, (const BYTE*)&dumpCount, sizeof(DWORD));
        ok = true;
        RegCloseKey(hApp);
    }
    RegCloseKey(hBase);
    return ok;
}

void SimulateCrash() {
    volatile int* p = nullptr;
    *p = 1; // AV
}

} // namespace CrashDumpWin

#pragma warning(pop)
#endif // _WIN32
