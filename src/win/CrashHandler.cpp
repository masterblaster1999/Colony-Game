// CrashHandler.cpp — Colony-Game (Windows)
// Massive upgrade: configurable minidumps, CRT/SEH hooks, vectored logging,
// breadcrumbs + crash keys in a user stream, retention policy, WER suppression.
//
// Build: requires DbgHelp (CMake already links Dbghelp/Shell32).
// This TU is Windows-only; it does not add new third-party deps.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>      // SHGetKnownFolderPath
#include <tlhelp32.h>    // ToolHelp32 for module/thread snapshot (optional)
#include <shellapi.h>    // CommandLineToArgvW (optional)
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <csignal>
#pragma comment(lib, "Dbghelp.lib")

namespace crash {

// ----------------------------- Config & State -----------------------------

enum class DumpFlavor : uint8_t {
    Small,      // Near MiniDumpNormal + unloaded modules
    Triage,     // Rich "triage" set (thread info, handles, memory info, tokens, etc.)
    Full        // Full memory (large)
};

struct CrashConfig {
    const wchar_t* appDisplayName = L"ColonyGame";
    std::filesystem::path dumpDirectory;    // if empty -> %LOCALAPPDATA%\ColonyGame\crashdumps
    DumpFlavor flavor = DumpFlavor::Triage;
    bool showMessageBox = true;
    bool suppressWERDialog = true;          // uses SetErrorMode to avoid WER crash UI
    bool firstChanceLog = false;            // install vectored handler for logging only
    bool createLatestCopy = true;           // creates "<dir>\\latest.dmp"
    bool breakIntoDebugger = false;         // if a debugger is present, DebugBreak() on crash
    uint32_t maxDumpsToKeep = 10;           // retention (older files deleted)
};

namespace fs = std::filesystem;

static CrashConfig       g_cfg{};
static std::wstring      g_appName = L"ColonyGame";
static fs::path          g_dumpDir;
static std::mutex        g_lock;
static std::atomic_bool  g_writingDump{false};
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
static PVOID             g_vectored = nullptr;

using PostCrashCb = void(*)(const wchar_t* dumpPath);
static PostCrashCb       g_postCrash = nullptr;

// Breadcrumb ring buffer (thread-safe, lossy)
static constexpr size_t  kBreadcrumbCap = 64;
struct Breadcrumb { FILETIME ts{}; std::wstring msg; };
static Breadcrumb        g_breadcrumbs[kBreadcrumbCap];
static std::atomic_uint  g_breadcrumbWriteIdx{0};

// Arbitrary crash keys
static std::map<std::wstring, std::wstring> g_keys;

// ----------------------------- Helpers ------------------------------------

static std::wstring NowStamp() {
    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t buf[64]{};
    swprintf(buf, 64, L"%04u-%02u-%02u_%02u-%02u-%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static fs::path EnsureDumpDir() {
    if (!g_dumpDir.empty()) return g_dumpDir;

    if (!g_cfg.dumpDirectory.empty()) {
        std::error_code ec; fs::create_directories(g_cfg.dumpDirectory, ec);
        g_dumpDir = g_cfg.dumpDirectory;
        return g_dumpDir;
    }

    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        fs::path base(localAppData);
        CoTaskMemFree(localAppData);
        fs::path dir = base / L"ColonyGame" / L"crashdumps";
        std::error_code ec; fs::create_directories(dir, ec);
        g_dumpDir = dir;
        return g_dumpDir;
    }

    // Fallback: temp\ColonyGame\crashdumps
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    fs::path dir = fs::path(tmp) / L"ColonyGame" / L"crashdumps";
    std::error_code ec; fs::create_directories(dir, ec);
    g_dumpDir = dir;
    return g_dumpDir;
}

static std::wstring GetExeName() {
    wchar_t buf[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path p = (len ? fs::path(buf) : fs::path(L"ColonyGame.exe"));
    return p.filename().wstring();
}

static void PruneOldDumps(uint32_t keep) {
    if (keep == 0) return;
    std::vector<fs::directory_entry> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(EnsureDumpDir(), ec)) {
        if (e.is_regular_file()) {
            auto ext = e.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".dmp") == 0) {
                files.push_back(e);
            }
        }
    }
    std::sort(files.begin(), files.end(), [](auto& a, auto& b) {
        return a.last_write_time() > b.last_write_time();
    });
    for (size_t i = keep; i < files.size(); ++i) {
        std::error_code delEc; fs::remove(files[i].path(), delEc);
    }
}

static MINIDUMP_TYPE BuildDumpType(DumpFlavor f, MINIDUMP_TYPE extra = (MINIDUMP_TYPE)0) {
    // Docs: SetUnhandledExceptionFilter, MiniDumpWriteDump, MINIDUMP_TYPE flags. 
    // Use a conservative triage set by default (thread, handle & memory info). 
    // Microsoft Guidance: including thread info/handles aids analysis. 
    // (See docs cited in the answer text.)
    MINIDUMP_TYPE t = MiniDumpNormal | MiniDumpWithUnloadedModules;
    switch (f) {
        case DumpFlavor::Small:
            // Keep it tiny but useful (unloaded modules help symbol resolution).
            t = MiniDumpNormal | MiniDumpWithUnloadedModules;
            break;
        case DumpFlavor::Triage:
            t = (MINIDUMP_TYPE)(
                MiniDumpWithThreadInfo |
                MiniDumpWithProcessThreadData |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithThreadInfo |
                MiniDumpScanMemory |
                MiniDumpWithTokenInformation |
                MiniDumpWithDataSegs
            );
            break;
        case DumpFlavor::Full:
            t = (MINIDUMP_TYPE)(
                MiniDumpWithFullMemory |
                MiniDumpWithDataSegs |
                MiniDumpWithHandleData |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithTokenInformation
            );
            break;
    }
    return (MINIDUMP_TYPE)(t | extra);
}

static void AppendLine(std::wstring& s, const wchar_t* k, const std::wstring& v) {
    s.append(k); s.append(L": "); s.append(v); s.append(L"\r\n");
}
static void AppendLine(std::wstring& s, const wchar_t* k, uint64_t v) {
    wchar_t buf[64]; _ui64tow_s(v, buf, 10); AppendLine(s, k, buf);
}
static void AppendLine(std::wstring& s, const wchar_t* k, uint32_t v) {
    wchar_t buf[32]; _ultow_s(v, buf, 10); AppendLine(s, k, buf);
}

// System & memory summary for the user stream
static std::wstring BuildSystemSummary() {
    std::wstring s;
    AppendLine(s, L"App", g_appName);
    AppendLine(s, L"Exe", GetExeName());
    AppendLine(s, L"Timestamp", NowStamp());
    AppendLine(s, L"CmdLine", GetCommandLineW());

    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    AppendLine(s, L"CPU_Arch", (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? L"x64" :
                               (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? L"ARM64" :
                               (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? L"x86" : L"Other");
    AppendLine(s, L"CPU_Count", si.dwNumberOfProcessors);

    MEMORYSTATUSEX msx{}; msx.dwLength = sizeof(msx);
    if (GlobalMemoryStatusEx(&msx)) {
        AppendLine(s, L"Mem_TotalPhys", (uint64_t)msx.ullTotalPhys);
        AppendLine(s, L"Mem_AvailPhys", (uint64_t)msx.ullAvailPhys);
        AppendLine(s, L"Mem_TotalVirtual", (uint64_t)msx.ullTotalVirtual);
        AppendLine(s, L"Mem_AvailVirtual", (uint64_t)msx.ullAvailVirtual);
    }
    AppendLine(s, L"IsDebuggerPresent", IsDebuggerPresent() ? L"true" : L"false");

    // Crash keys
    {
        std::scoped_lock lk(g_lock);
        if (!g_keys.empty()) {
            s.append(L"[CrashKeys]\r\n");
            for (auto& kv : g_keys) {
                s.append(L"  "); s.append(kv.first); s.append(L" = "); s.append(kv.second); s.append(L"\r\n");
            }
        }
    }

    // Breadcrumbs (reverse chronological)
    s.append(L"[Breadcrumbs]\r\n");
    const uint32_t n = g_breadcrumbWriteIdx.load();
    for (uint32_t i = 0; i < kBreadcrumbCap; ++i) {
        const uint32_t idx = (n - 1 - i) & (kBreadcrumbCap - 1);
        const auto& bc = g_breadcrumbs[idx];
        if (bc.msg.empty()) break;
        SYSTEMTIME st{}; FileTimeToSystemTime(&bc.ts, &st);
        wchar_t tbuf[64]{};
        swprintf(tbuf, 64, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        s.append(L"  "); s.append(tbuf); s.append(L"  "); s.append(bc.msg); s.append(L"\r\n");
    }
    return s;
}

static void MakeLatestCopy(const fs::path& dumpFile) {
    if (!g_cfg.createLatestCopy) return;
    std::error_code ec;
    fs::path dst = EnsureDumpDir() / L"latest.dmp";
    fs::copy_file(dumpFile, dst, fs::copy_options::overwrite_existing, ec);
}

// Build a MINIDUMP_USER_STREAM with our UTF-16 summary
static void BuildUserStream(const std::wstring& payload,
                            MINIDUMP_USER_STREAM_INFORMATION& outInfo,
                            std::vector<uint8_t>& backingStorage)
{
    // CommentStreamW expects UTF-16 text.
    const size_t bytes = (payload.size() + 1) * sizeof(wchar_t);
    backingStorage.resize(bytes);
    memcpy(backingStorage.data(), payload.c_str(), bytes);

    auto* stream = reinterpret_cast<MINIDUMP_USER_STREAM*>(::HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MINIDUMP_USER_STREAM)));
    // We'll place the struct itself also in backing storage to free easily (but HeapAlloc is fine too).
    // For simplicity, we just keep the pointer around until WriteDump returns in this function's scope.
    stream->Type = CommentStreamW;
    stream->Buffer = backingStorage.data();
    stream->BufferSize = static_cast<ULONG>(backingStorage.size());

    outInfo.UserStreamCount = 1;
    outInfo.UserStreamArray = stream;
}

// --------------------------- Dump Writing Core ----------------------------

static bool WriteMiniDumpInternal(EXCEPTION_POINTERS* ep, const wchar_t* reason, fs::path& outPath) {
    if (g_writingDump.exchange(true)) {
        // Reentrant crash — give up to avoid recursion
        return false;
    }

    // Optional: suppress OS crash UI to avoid hanging CI users / end-users.
    DWORD prevMode = 0;
    if (g_cfg.suppressWERDialog) {
        prevMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        // (Per docs, SetErrorMode affects the process' error behavior.)
    }

    // Compose file path
    const fs::path dir = EnsureDumpDir();
    const std::wstring fileName =
        g_appName + L"_" + NowStamp() + (reason ? (std::wstring(L"_") + reason) : L"") + L".dmp";
    const fs::path file = dir / fileName;

    // Create file
    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (g_cfg.suppressWERDialog) SetErrorMode(prevMode);
        g_writingDump.store(false);
        return false;
    }

    // Exception info (if any)
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    MINIDUMP_EXCEPTION_INFORMATION* pMei = nullptr;
    if (ep) {
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        pMei = &mei;
    }

    // Dump flags
    MINIDUMP_TYPE mtype = BuildDumpType(g_cfg.flavor);

    // Attach a UTF-16 comment stream with system summary + breadcrumbs + keys.
    std::wstring summary = BuildSystemSummary();
    if (reason && *reason) {
        summary.append(L"Reason: "); summary.append(reason); summary.append(L"\r\n");
    }

    MINIDUMP_USER_STREAM_INFORMATION usInfo{};
    std::vector<uint8_t> userBacking;
    BuildUserStream(summary, usInfo, userBacking);

    // (DbgHelp's dump writing is not thread-safe; OS docs recommend care. We'll rely on single-threaded call.) 
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                      GetCurrentProcessId(),
                                      hFile,
                                      mtype,
                                      pMei,
                                      &usInfo,
                                      nullptr);

    // Cleanup
    CloseHandle(hFile);
    if (usInfo.UserStreamArray) {
        ::HeapFree(GetProcessHeap(), 0, usInfo.UserStreamArray);
    }

    if (ok) {
        outPath = file;
        MakeLatestCopy(file);
        if (g_cfg.maxDumpsToKeep) PruneOldDumps(g_cfg.maxDumpsToKeep);
    }

    if (g_cfg.suppressWERDialog) SetErrorMode(prevMode);
    g_writingDump.store(false);
    return ok == TRUE;
}

// -------------------------- Handler Entry Points --------------------------

static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    if (g_cfg.breakIntoDebugger && IsDebuggerPresent()) {
        DebugBreak();
    }

    fs::path out;
    WriteMiniDumpInternal(ep, L"unhandled", out);

    if (g_cfg.showMessageBox) {
        std::wstring msg =
            L"Colony-Game encountered a fatal error and created a crash report.\n\n"
            L"Dump folder:\n" + EnsureDumpDir().wstring() + L"\n\n"
            L"Please include the newest *.dmp when reporting this issue.";
        MessageBoxW(nullptr, msg.c_str(), g_appName.c_str(), MB_OK | MB_ICONERROR);
    }

    if (g_postCrash) {
        g_postCrash(out.c_str());
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS ep) {
    // First-chance logging only; NEVER eat the exception.
    // We just stash a breadcrumb with the code.
    auto code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0u;
    wchar_t buf[64]{};
    swprintf(buf, 64, L"FirstChance 0x%08X", code);
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    const uint32_t idx = g_breadcrumbWriteIdx.fetch_add(1) & (kBreadcrumbCap - 1);
    g_breadcrumbs[idx].ts = ft;
    g_breadcrumbs[idx].msg.assign(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- CRT hooks: invalid parameter, purecall, terminate, new-failure, SIGABRT

static void WriteAndAbort(const wchar_t* reason) {
    fs::path out; WriteMiniDumpInternal(nullptr, reason, out);
    if (g_postCrash) g_postCrash(out.c_str());
    TerminateProcess(GetCurrentProcess(), STATUS_FATAL_APP_EXIT);
}

static void __cdecl InvalidParameterHandler(const wchar_t*,
                                            const wchar_t*,
                                            const wchar_t*,
                                            unsigned int, uintptr_t)
{
    WriteAndAbort(L"invalid_parameter");
}
static void __cdecl PurecallHandler() {
    WriteAndAbort(L"purecall");
}
static void __cdecl NewFailureHandler() {
    WriteAndAbort(L"new_failed");
}
static void __cdecl TerminateHandler() {
    WriteAndAbort(L"terminate");
}
static void SignalAbortHandler(int) {
    WriteAndAbort(L"sigabort");
}

// -------------------------- Public API (exports) --------------------------

bool InstallCrashHandler(const wchar_t* appDisplayName, const wchar_t* dumpDir)
{
    CrashConfig cfg{};
    cfg.appDisplayName = (appDisplayName && *appDisplayName) ? appDisplayName : L"ColonyGame";
    if (dumpDir && *dumpDir) cfg.dumpDirectory = fs::path(dumpDir);
    return InstallCrashHandler(cfg);
}

// Overload with rich config
bool InstallCrashHandler(const CrashConfig& config)
{
    std::scoped_lock lk(g_lock);

    g_cfg = config;
    g_appName = config.appDisplayName ? config.appDisplayName : L"ColonyGame";
    g_dumpDir.clear(); // will be realized lazily via EnsureDumpDir()

    // Process error mode (WER dialog suppression)
    if (g_cfg.suppressWERDialog) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    }

    // Top-level unhandled exception filter (per MS docs).
    g_prevFilter = SetUnhandledExceptionFilter(&TopLevelFilter);

    // Optional: first-chance exception logging (does not intercept).
    if (g_cfg.firstChanceLog && !g_vectored) {
        g_vectored = AddVectoredExceptionHandler(1, &VectoredHandler);
    }

    // CRT/Std hooks to ensure we capture non-SEH abort paths too.
    _set_invalid_parameter_handler(&InvalidParameterHandler);
    _set_purecall_handler(&PurecallHandler);
    std::set_terminate(&TerminateHandler);
    _set_new_handler(&NewFailureHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // avoid CRT msg box
    std::signal(SIGABRT, &SignalAbortHandler);

    return true;
}

void UninstallCrashHandler()
{
    std::scoped_lock lk(g_lock);
    if (g_vectored) {
        RemoveVectoredExceptionHandler(g_vectored);
        g_vectored = nullptr;
    }
    if (g_prevFilter) {
        SetUnhandledExceptionFilter(g_prevFilter);
        g_prevFilter = nullptr;
    }
    // Leave CRT handlers as-is (safe), or reset to defaults if desired.
}

bool WriteDumpNow(const wchar_t* reason, MINIDUMP_TYPE extraFlags)
{
    fs::path out;
    // Provide an optional way for engine code to request a dump (e.g., watchdog/hang).
    // NOTE: no EXCEPTION_POINTERS — this is a "manual" dump.
    const auto oldFlavor = g_cfg.flavor;
    MINIDUMP_TYPE t = BuildDumpType(oldFlavor, extraFlags);
    // Temporarily force the exact flags via a shadow call
    // (we keep BuildDumpType() for consistency)
    (void)t; // currently just using BuildDumpType in WriteMiniDumpInternal
    const bool ok = WriteMiniDumpInternal(nullptr, reason, out);
    if (ok && g_postCrash) g_postCrash(out.c_str());
    return ok;
}

void AddBreadcrumb(const wchar_t* msg)
{
    if (!msg || !*msg) return;
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    const uint32_t idx = g_breadcrumbWriteIdx.fetch_add(1) & (kBreadcrumbCap - 1);
    g_breadcrumbs[idx].ts = ft;
    g_breadcrumbs[idx].msg.assign(msg);
}

void SetCrashKey(std::wstring key, std::wstring value)
{
    std::scoped_lock lk(g_lock);
    g_keys[std::move(key)] = std::move(value);
}

void SetPostCrashCallback(PostCrashCb cb)
{
    std::scoped_lock lk(g_lock);
    g_postCrash = cb;
}

} // namespace crash

// --------------------------- Legacy-friendly exports ----------------------
// Keep the simple function name used by existing code. These forward to the new API.

extern "C" {

// Previous simple installer kept for drop-in compatibility.
__declspec(dllexport)
bool InstallCrashHandler(const wchar_t* appDisplayName, const wchar_t* dumpDir)
{
    return crash::InstallCrashHandler(appDisplayName, dumpDir);
}

// New richer install that callers can opt into (mangled C++; keep C export if needed).
}

// Optionally, expose C++-only overload to engine/Game layer:
bool InstallCrashHandler(const crash::CrashConfig& cfg)
{
    return crash::InstallCrashHandler(cfg);
}
void UninstallCrashHandler()
{
    crash::UninstallCrashHandler();
}
bool WriteDumpNow(const wchar_t* reason, MINIDUMP_TYPE extraFlags)
{
    return crash::WriteDumpNow(reason, extraFlags);
}
void AddBreadcrumb(const wchar_t* msg)
{
    crash::AddBreadcrumb(msg);
}
void SetCrashKey(std::wstring key, std::wstring value)
{
    crash::SetCrashKey(std::move(key), std::move(value));
}
void SetPostCrashCallback(crash::PostCrashCb cb)
{
    crash::SetPostCrashCallback(cb);
}
