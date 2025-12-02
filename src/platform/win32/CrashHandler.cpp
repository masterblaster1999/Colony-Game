// CrashHandler.cpp - Windows-only crash and diagnostics utilities for Colony-Game
// Public API preserved: InstallCrashHandler / UninstallCrashHandler / LogLine
//
// Major upgrades in this version:
//  - Fixed namespace bug: use winqol::ExeDir() (C3861) and winqol::EnsureDir(...).
//  - Richer boot header: OS version/build, CPU brand string, module count.
//  - Better stack traces: sets a sensible symbol search path and refreshes modules.
//  - Mini-dump robustness: writes a diagnostic-rich dump and logs precise errors.
//  - First-chance exception throttling + clearer unhandled crash info.
//  - Automatic log/dump retention (config constants below).
//  - Optional clipboard copy of dump folder on crash prompt acceptance.
//  - Unity/Jumbo build safety: no duplicate EnsureDir and fewer anonymous helpers.

#include "CrashHandler.h"
#include "AppPaths.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winreg.h>
#include <dbghelp.h>      // MiniDumpWriteDump, symbol APIs
#include <crtdbg.h>       // _set_invalid_parameter_handler, _set_purecall_handler, _set_abort_behavior
#include <eh.h>           // _set_se_translator (optional)
#include <shellapi.h>     // ShellExecuteW
#include <shlobj.h>       // SHGetKnownFolderPath (via AppPaths, if needed)
#include <TlHelp32.h>     // Toolhelp32 for module enumeration
#include <VersionHelpers.h>
#include <intrin.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace {

namespace fs = std::filesystem;

// -------- Retention policy (tweak as desired; public API unchanged) --------
constexpr size_t kRetainLogs  = 10;
constexpr size_t kRetainDumps = 10;

// -------- Internal state (renamed to avoid Unity/Jumbo collisions) --------
std::wofstream    s_crashLog;
std::mutex        s_crashLogMutex;
std::wstring      s_appName;
std::wstring      s_appVersion;
std::wstring      s_dumpDir;
std::wstring      s_logsDir;
std::once_flag    s_installOnce;
std::atomic<bool> s_inUnhandled{false};
PVOID             s_vectoredHandler = nullptr;
bool              s_dbghelpReady = false;

// -------- Small utilities --------
std::wstring NowTimestamp(bool with_ms = true) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    std::tm tm_{}; localtime_s(&tm_, &t);

    std::wstringstream ss;
    if (with_ms) {
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        ss << std::put_time(&tm_, L"%Y%m%d-%H%M%S") << L"." << std::setw(3) << std::setfill(L'0') << ms.count();
    } else {
        ss << std::put_time(&tm_, L"%Y%m%d-%H%M%S");
    }
    return ss.str();
}

std::wstring LastErrorToString(DWORD err) {
    if (err == 0) return L"(no error)";
    LPWSTR buf = nullptr;
    const DWORD flags  = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    const DWORD len = FormatMessageW(flags, nullptr, err, langId, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = len ? std::wstring(buf, len) : L"(unknown)";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) msg.pop_back();
    std::wstringstream ss; ss << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << err << L" - " << msg;
    return ss.str();
}

// Narrow->wide helper (for CPUID brand string)
std::wstring ToWide(const char* s) {
    if (!s || !*s) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (needed <= 0) {
        // try ANSI as fallback
        needed = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        if (needed <= 0) return {};
        std::wstring w; w.resize(static_cast<size_t>(needed - 1));
        MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), needed);
        return w;
    }
    std::wstring w; w.resize(static_cast<size_t>(needed - 1));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), needed);
    return w;
}

std::wstring CpuBrandString() {
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = static_cast<unsigned int>(cpuInfo[0]);
    char brand[0x40] = {};
    if (nExIds >= 0x80000004) {
        for (unsigned int i = 0; i < 3; ++i) {
            __cpuid(cpuInfo, 0x80000002 + i);
            std::memcpy(brand + i * 16, cpuInfo, 16);
        }
    } else {
        // Vendor ID fallback
        __cpuid(cpuInfo, 0);
        int vendor[3] = { cpuInfo[1], cpuInfo[3], cpuInfo[2] }; // EBX, EDX, ECX
        std::memcpy(brand, vendor, 12);
        brand[12] = '\0';
    }
    // Trim leading/trailing spaces
    std::wstring w = ToWide(brand);
    while (!w.empty() && w.front() == L' ') w.erase(w.begin());
    while (!w.empty() && w.back()  == L' ') w.pop_back();
    return w;
}

// --- Replaced GetVersionExW with VersionHelpers + registry read for display/build ---
std::wstring OsVersionString() {
    // Read Windows release identifiers from the documented registry keys and
    // combine with VersionHelpers buckets for a user-friendly string.
    constexpr const wchar_t* kKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    auto readSz = [](HKEY root, const wchar_t* subkey, const wchar_t* name) -> std::wstring {
        HKEY h = nullptr;
        std::wstring value;
        // Prefer the 64-bit view when available; fall back otherwise.
        if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
            if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS)
                return value;
        }
        DWORD type = 0, bytes = 0;
        if (RegGetValueW(h, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            bytes >= sizeof(wchar_t)) {
            value.resize((bytes / sizeof(wchar_t)) - 1);
            if (RegGetValueW(h, nullptr, name, RRF_RT_REG_SZ, &type, value.data(), &bytes) != ERROR_SUCCESS) {
                value.clear();
            }
        }
        RegCloseKey(h);
        return value;
    };

    std::wstring display = readSz(HKEY_LOCAL_MACHINE, kKey, L"DisplayVersion");   // e.g. 24H2 / 23H2
    if (display.empty())
        display = readSz(HKEY_LOCAL_MACHINE, kKey, L"ReleaseId");                 // older Win10 fallback
    std::wstring build   = readSz(HKEY_LOCAL_MACHINE, kKey, L"CurrentBuildNumber");

    std::wstring result = L"Windows ";

    if (IsWindows10OrGreater()) {
        result += !display.empty() ? display : L"10/11";
    } else if (IsWindows8Point1OrGreater()) {
        result += L"8.1";
    } else if (IsWindows8OrGreater()) {
        result += L"8";
    } else if (IsWindows7SP1OrGreater()) {
        result += L"7 SP1";
    } else {
        result += L"(unknown)";
    }

    if (!build.empty()) {
        result += L" (Build ";
        result += build;
        result += L")";
    }

    return result;
}

void SafeFlushLog() {
    std::scoped_lock lock(s_crashLogMutex);
    if (s_crashLog.is_open()) s_crashLog.flush();
}

void LogLineUnlocked(const std::wstring& line) {
    if (s_crashLog.is_open()) {
        s_crashLog << L"[" << NowTimestamp(false) << L"] " << line << L"\n";
        s_crashLog.flush();
    }
    OutputDebugStringW((line + L"\n").c_str());
}

void LogLineInternal(const std::wstring& line) {
    std::scoped_lock lock(s_crashLogMutex);
    LogLineUnlocked(line);
}

// Enumerate loaded modules with Toolhelp32 (best-effort for quick triage)
void LogLoadedModules() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        LogLineInternal(L"[BOOT] Module snapshot failed: " + LastErrorToString(GetLastError()));
        return;
    }
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    std::vector<MODULEENTRY32W> mods;
    if (Module32FirstW(snap, &me)) {
        do { mods.push_back(me); } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    std::scoped_lock lock(s_crashLogMutex);
    if (!s_crashLog.is_open()) return;
    s_crashLog << L"[BOOT] Modules loaded: " << mods.size() << L"\n";
    size_t count = 0;
    for (const auto& m : mods) {
        if (count++ > 64) { s_crashLog << L"    ... (" << (mods.size() - count) << L" more)\n"; break; }
        s_crashLog << L"    " << (m.szModule ? m.szModule : L"(?)")
                   << L" @ 0x" << std::hex << reinterpret_cast<uintptr_t>(m.modBaseAddr) << std::dec
                   << L", size=" << m.modBaseSize
                   << L", path=" << (m.szExePath ? m.szExePath : L"(?)") << L"\n";
    }
    s_crashLog.flush();
}

// Delete older files matching appName_*.<ext>, keeping most recent `keep`
void PruneOldFiles(const std::wstring& dir, const std::wstring& prefix, const std::wstring& ext, size_t keep) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    struct Item { fs::path path; fs::file_time_type t; };
    std::vector<Item> items;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto& p = entry.path();
        const auto fname = p.filename().wstring();
        if (fname.size() >= prefix.size() + ext.size() &&
            fname.rfind(ext) == fname.size() - ext.size() &&
            fname.find(prefix) == 0) {
            items.push_back({p, entry.last_write_time(ec)});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){ return a.t > b.t; });
    if (items.size() <= keep) return;

    for (size_t i = keep; i < items.size(); ++i) {
        std::error_code rmec;
        fs::remove(items[i].path, rmec);
    }
}

// Initialize DbgHelp for symbolization (best-effort)
bool EnsureDbgHelp() {
    if (s_dbghelpReady) return true;

    // Compose a helpful symbol search path: exe dir; current dir; logs dir
    std::wstring symPath = L".;" + winqol::ExeDir();
    if (!s_logsDir.empty()) symPath += L";" + s_logsDir;

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymSetSearchPathW(GetCurrentProcess(), symPath.c_str())) {
        LogLineInternal(L"[SYMS] SymSetSearchPath failed: " + LastErrorToString(GetLastError()));
    }

    if (SymInitializeW(GetCurrentProcess(), nullptr, TRUE)) {
        s_dbghelpReady = true;
        SymRefreshModuleList(GetCurrentProcess());
    } else {
        LogLineInternal(L"[SYMS] SymInitialize failed: " + LastErrorToString(GetLastError()));
    }
    return s_dbghelpReady;
}

// Basic stack trace logging (best-effort; symbols if PDBs available)
void LogStackTrace(const wchar_t* caption = L"Stack") {
    constexpr USHORT kMaxFrames = 62;
    void* frames[kMaxFrames]{};

    USHORT captured = RtlCaptureStackBackTrace(0, kMaxFrames, frames, nullptr);
    if (captured == 0) {
        LogLineInternal(L"[TRACE] No stack frames captured.");
        return;
    }

    EnsureDbgHelp();

    std::scoped_lock lock(s_crashLogMutex);
    if (s_crashLog.is_open()) {
        s_crashLog << L"[TRACE] " << caption << L" (" << captured << L" frames)\n";
        if (s_dbghelpReady) {
            // SYMBOL_INFOW is variable-length; allocate room for max name
            const size_t bytes = sizeof(SYMBOL_INFOW) + (MAX_SYM_NAME + 1) * sizeof(wchar_t);
            std::unique_ptr<unsigned char[]> buf(new unsigned char[bytes]);
            auto* sym = reinterpret_cast<SYMBOL_INFOW*>(buf.get());
            sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
            sym->MaxNameLen   = MAX_SYM_NAME;

            IMAGEHLP_LINEW64 line{};
            line.SizeOfStruct = sizeof(line);

            for (USHORT i = 0; i < captured; ++i) {
                DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
                DWORD64 disp = 0;
                DWORD   lineDisp = 0;

                bool haveName = !!SymFromAddrW(GetCurrentProcess(), addr, &disp, sym);
                bool haveLine = !!SymGetLineFromAddrW64(GetCurrentProcess(), addr, &lineDisp, &line);

                s_crashLog << L"    [" << i << L"] 0x" << std::hex << addr << std::dec;
                if (haveName) s_crashLog << L" : " << sym->Name << L"+0x" << std::hex << disp << std::dec;
                if (haveLine) s_crashLog << L" (" << line.FileName << L":" << line.LineNumber << L")";
                s_crashLog << L"\n";
            }
        } else {
            for (USHORT i = 0; i < captured; ++i) {
                DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
                s_crashLog << L"    [" << i << L"] 0x" << std::hex << addr << std::dec << L"\n";
            }
        }
        s_crashLog.flush();
    }
}

void AppendLogHeader_NoLock() {
    if (!s_crashLog.is_open()) return;

    // Write BOM so editors detect UTF-16LE correctly
    static bool wroteBOM = false;
    if (!wroteBOM) {
        const wchar_t bom = 0xFEFF;
        s_crashLog.write(&bom, 1);
        wroteBOM = true;
    }

    s_crashLog << L"[BOOT] " << s_appName << L" v" << s_appVersion << L"\n";
    s_crashLog << L"[BOOT] exe dir: " << winqol::ExeDir() << L"\n";
    s_crashLog << L"[BOOT] OS: " << OsVersionString() << L"\n";
    s_crashLog << L"[BOOT] CPU: " << CpuBrandString() << L"\n";

    // OS & memory hints
    MEMORYSTATUSEX msx{}; msx.dwLength = sizeof(msx);
    if (GlobalMemoryStatusEx(&msx)) {
        s_crashLog << L"[BOOT] RAM total: " << (msx.ullTotalPhys / (1024ull * 1024ull)) << L" MiB, "
                   << L"avail: " << (msx.ullAvailPhys / (1024ull * 1024ull)) << L" MiB\n";
    }
    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    s_crashLog << L"[BOOT] CPU cores: " << si.dwNumberOfProcessors << L"\n";
    s_crashLog.flush();

    LogLoadedModules();
}

// Compose a dump filename and write it; returns path via outPath if provided
void WriteDump(EXCEPTION_POINTERS* ep, std::wstring* outPath = nullptr) {
    if (s_dumpDir.empty()) return;

    // Ensure destination exists (use public helper to avoid duplicate implementations)
    winqol::EnsureDir(s_dumpDir);

    const std::wstring file = s_dumpDir + L"\\" + s_appName + L"_" + NowTimestamp(true) + L".dmp";

    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogLineInternal(L"[CRASH] Failed to create dump file: " + file + L" (" + LastErrorToString(GetLastError()) + L")");
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    // Reasonably rich dump while keeping size sane for user support
    const MINIDUMP_TYPE type =
        static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithDataSegs |
            MiniDumpWithThreadInfo |
            MiniDumpWithHandleData |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithUnloadedModules |
            MiniDumpIgnoreInaccessibleMemory
        );

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(),
        hFile, type, ep ? &mei : nullptr, nullptr, nullptr
    );
    const DWORD dumpErr = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(hFile);

    if (ok) {
        LogLineInternal(L"[CRASH] Minidump written: " + file);
        if (outPath) *outPath = file;
    } else {
        LogLineInternal(L"[CRASH] MiniDumpWriteDump failed: " + LastErrorToString(dumpErr));
    }
}

// Copy text to clipboard (best-effort)
void CopyTextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* dst = GlobalLock(hMem);
        if (dst) {
            std::memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

// Unhandled exception filter
LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    // Re-entry guard: if we crash while crashing, just return to OS
    bool expected = false;
    if (!s_inUnhandled.compare_exchange_strong(expected, true)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    {
        std::scoped_lock lock(s_crashLogMutex);
        if (s_crashLog.is_open()) {
            s_crashLog << L"[CRASH] Unhandled exception"
                       << (code ? (std::wstring(L" code=0x") + [] (DWORD c){ std::wstringstream ss; ss<<std::hex<<c; return ss.str(); }(code)) : L"")
                       << (addr ? (std::wstring(L" at 0x") + [] (const void* p){ std::wstringstream ss; ss<<std::hex<<reinterpret_cast<uintptr_t>(p); return ss.str(); }(addr)) : L"")
                       << L".\n";
            s_crashLog.flush();
        }
    }

    LogStackTrace(L"Unhandled");

    std::wstring dumpPath;
    WriteDump(ep, &dumpPath);

    // Offer to open the dumps folder. Keep UX simple and non-blocking.
    if (!IsDebuggerPresent()) {
        const std::wstring msg =
            L"Colony-Game encountered a fatal error and must close.\n\n"
            L"A crash report (.dmp) and log were written to:\n"
            L"  " + s_dumpDir + L"\n\n"
            L"Click Yes to open the folder. The path has been copied to your clipboard.";
        CopyTextToClipboard(s_dumpDir);
        const int button = MessageBoxW(nullptr, msg.c_str(), L"Colony-Game Crash",
                                       MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
        if (button == IDYES) {
            ShellExecuteW(nullptr, L"open", s_dumpDir.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

// CRT hooks
void __cdecl InvalidParamHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    LogLineInternal(L"[CRT] Invalid parameter handler fired.");
    LogStackTrace(L"Invalid Parameter");
}

void __cdecl PurecallHandler() {
    LogLineInternal(L"[CRT] Pure virtual function call.");
    LogLineInternal(L"[CRT] This usually indicates a call in a ctor/dtor to a pure-virtual.");
    LogStackTrace(L"Purecall");
}

// Optional: translate SEH into C++ exceptions when caught in try/catch, but always log
void __cdecl SehTranslator(unsigned int code, _EXCEPTION_POINTERS* ep) {
    std::wstringstream ss; ss << L"[SEH] Structured exception code: 0x" << std::hex << code;
    LogLineInternal(ss.str());
    (void)ep; // We don't rethrow here; translator installed primarily to ensure logging.
}

// First-chance vectored exception handler (rate-limited to avoid log spam)
LONG CALLBACK FirstChanceVEH(EXCEPTION_POINTERS* ep) {
    static std::atomic<uint32_t> s_firstChanceCount{0};
    const uint32_t n = s_firstChanceCount.fetch_add(1, std::memory_order_relaxed);
    if (n < 10) { // log the first few only
        std::wstringstream ss;
        const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        ss << L"[EXC] First-chance exception code=0x" << std::hex << code;
        LogLineInternal(ss.str());
    }
    return EXCEPTION_CONTINUE_SEARCH; // do not swallow
}

} // anonymous namespace

// ----------------------------- Public API ---------------------------------

namespace winqol {

void InstallCrashHandler(const std::wstring& appName, const std::wstring& appVersion) {
    std::call_once(s_installOnce, [&] {
        s_appName    = appName;
        s_appVersion = appVersion;
        s_logsDir    = LogsDir(appName);
        s_dumpDir    = DumpsDir(appName);

        // Use public EnsureDir to avoid duplicate local implementations (unity-safe)
        winqol::EnsureDir(s_logsDir);
        winqol::EnsureDir(s_dumpDir);

        // Retention: prune older logs/dumps so support bundles stay small
        PruneOldFiles(s_logsDir, appName + L"_", L".log", kRetainLogs);
        PruneOldFiles(s_dumpDir, appName + L"_", L".dmp", kRetainDumps);

        // Open timestamped log (UTF-16LE via wofstream). Use binary to control BOM.
        const std::wstring logPath = s_logsDir + L"\\" + appName + L"_" + NowTimestamp(true) + L".log";
        s_crashLog.open(logPath, std::ios::out | std::ios::trunc | std::ios::binary);
        {
            std::scoped_lock lock(s_crashLogMutex);
            AppendLogHeader_NoLock();
            LogLineUnlocked(L"[BOOT] log: " + logPath);
        }

        // Harden process & avoid disruptive error UI
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        // Reduce CRT popups on abort; keep everything in our logs/minidumps
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

        // Install handlers early
        SetUnhandledExceptionFilter(&Unhandled);
        _set_invalid_parameter_handler(&InvalidParamHandler);
        _set_purecall_handler(&PurecallHandler);
        _set_se_translator(&SehTranslator);

        // First-chance diagnostics (non-fatal)
        s_vectoredHandler = AddVectoredExceptionHandler(1 /* call first */, &FirstChanceVEH);

        // Prepare DbgHelp after we've logged boot info
        EnsureDbgHelp();

        LogLineInternal(L"[BOOT] Crash handler installed.");
    });
}

void UninstallCrashHandler() {
    // Remove vectored handler if we added one
    if (s_vectoredHandler) {
        RemoveVectoredExceptionHandler(s_vectoredHandler);
        s_vectoredHandler = nullptr;
    }

    LogLineInternal(L"[BOOT] Crash handler uninstalling.");
    SafeFlushLog();

    if (s_dbghelpReady) {
        SymCleanup(GetCurrentProcess());
        s_dbghelpReady = false;
    }

    std::scoped_lock lock(s_crashLogMutex);
    if (s_crashLog.is_open()) s_crashLog.close();
}

void LogLine(const std::wstring& line) {
    LogLineInternal(line);
}

} // namespace winqol
