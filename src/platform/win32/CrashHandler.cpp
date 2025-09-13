// CrashHandler.cpp - Windows-only crash and diagnostics utilities for Colony-Game
// Public API preserved: InstallCrashHandler / UninstallCrashHandler / LogLine

#include "CrashHandler.h"
#include "AppPaths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dbghelp.h>      // MiniDumpWriteDump, symbol APIs
#include <crtdbg.h>       // _set_invalid_parameter_handler, _set_purecall_handler
#include <eh.h>           // _set_se_translator (optional)
#include <shlobj.h>       // SHCreateDirectoryExW
#include <shellapi.h>     // ShellExecuteW

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

// -------- Internal state (renamed to avoid Unity/Jumbo collisions) --------
std::wofstream s_crashLog;
std::mutex     s_crashLogMutex;
std::wstring   s_appName;
std::wstring   s_appVersion;
std::wstring   s_dumpDir;
std::wstring   s_logsDir;
std::once_flag s_installOnce;
std::atomic<bool> s_inUnhandled{false};
PVOID          s_vectoredHandler = nullptr;
bool           s_dbghelpReady = false;

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
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    const DWORD len = FormatMessageW(flags, nullptr, err, langId, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = len ? std::wstring(buf, len) : L"(unknown)";
    if (buf) LocalFree(buf);
    // Trim trailing CRLFs from FormatMessage
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) msg.pop_back();
    std::wstringstream ss; ss << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << err << L" - " << msg;
    return ss.str();
}

bool EnsureDir(const std::wstring& path) {
    if (path.empty()) return false;
    const int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    // Returns ERROR_ALREADY_EXISTS when directory is present; treat as success
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS;
}

void SafeFlushLog() {
    std::scoped_lock lock(s_crashLogMutex);
    if (s_crashLog.is_open()) s_crashLog.flush();
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
    s_crashLog << L"[BOOT] exe dir: " << ExeDir() << L"\n";

    // OS & memory hints
    // (These are safe calls and don't require manifests; they provide useful but generic info)
    MEMORYSTATUSEX msx{}; msx.dwLength = sizeof(msx);
    if (GlobalMemoryStatusEx(&msx)) {
        s_crashLog << L"[BOOT] RAM total: " << (msx.ullTotalPhys / (1024ull * 1024ull)) << L" MiB, "
                   << L"avail: " << (msx.ullAvailPhys / (1024ull * 1024ull)) << L" MiB\n";
    }
    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    s_crashLog << L"[BOOT] CPU cores: " << si.dwNumberOfProcessors << L"\n";
    s_crashLog.flush();
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

// Initialize DbgHelp for symbolization (best-effort)
bool EnsureDbgHelp() {
    if (s_dbghelpReady) return true;
    // SymInitialize is per-process; keep options sensible
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (SymInitializeW(GetCurrentProcess(), nullptr, TRUE)) {
        s_dbghelpReady = true;
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

// Compose a dump filename and write it; returns path via outPath if provided
void WriteDump(EXCEPTION_POINTERS* ep, std::wstring* outPath = nullptr) {
    if (s_dumpDir.empty()) return;

    EnsureDir(s_dumpDir);

    const std::wstring file =
        s_dumpDir + L"\\" + s_appName + L"_" + NowTimestamp(true) + L".dmp";

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

    // Richer dump while still reasonable in size for end users
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

    CloseHandle(hFile);

    if (ok) {
        LogLineInternal(L"[CRASH] Minidump written: " + file);
        if (outPath) *outPath = file;
    } else {
        LogLineInternal(L"[CRASH] MiniDumpWriteDump failed: " + LastErrorToString(GetLastError()));
    }
}

// Unhandled exception filter
LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    // Re-entry guard: if we crash while crashing, just return to OS
    bool expected = false;
    if (!s_inUnhandled.compare_exchange_strong(expected, true)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    {
        std::scoped_lock lock(s_crashLogMutex);
        if (s_crashLog.is_open()) {
            s_crashLog << L"[CRASH] Unhandled exception.\n";
            s_crashLog.flush();
        }
    }

    LogStackTrace(L"Unhandled");

    std::wstring dumpPath;
    WriteDump(ep, &dumpPath);

    // Offer to open the dumps folder. Keep UX simple and non-blocking.
    const std::wstring msg =
        L"Colony-Game encountered a fatal error and must close.\n\n"
        L"A crash report (.dmp) and log were written to:\n"
        L"  " + s_dumpDir + L"\n\n"
        L"Open the dumps folder now?";
    const int button = MessageBoxW(nullptr, msg.c_str(), L"Colony-Game Crash",
                                   MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST);
    if (button == IDYES) {
        ShellExecuteW(nullptr, L"open", s_dumpDir.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
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
    LogStackTrace(L"Purecall");
}

// Optional: translate SEH into C++ exceptions when caught in try/catch, but always log
void __cdecl SehTranslator(unsigned int code, _EXCEPTION_POINTERS* ep) {
    std::wstringstream ss; ss << L"[SEH] Structured exception code: 0x" << std::hex << code;
    LogLineInternal(ss.str());
    // We don't rethrow here; this translator is installed mostly to make sure we log rich info.
    (void)ep;
}

// First-chance vectored exception handler (rate-limited to avoid log spam)
LONG CALLBACK FirstChanceVEH(EXCEPTION_POINTERS* ep) {
    static std::atomic<uint32_t> s_firstChanceCount{0};
    const uint32_t n = s_firstChanceCount.fetch_add(1, std::memory_order_relaxed);
    if (n < 10) { // log the first few only
        std::wstringstream ss;
        ss << L"[EXC] First-chance exception code=0x" << std::hex << ep->ExceptionRecord->ExceptionCode;
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

        EnsureDir(s_logsDir);
        EnsureDir(s_dumpDir);

        // Open timestamped log (UTF-16LE via wofstream). Use binary to keep control of BOM.
        const std::wstring logPath = s_logsDir + L"\\" + appName + L"_" + NowTimestamp(true) + L".log";
        s_crashLog.open(logPath, std::ios::out | std::ios::trunc | std::ios::binary);
        {
            std::scoped_lock lock(s_crashLogMutex);
            AppendLogHeader_NoLock();
            LogLineUnlocked(L"[BOOT] log: " + logPath);
        }

        // Harden process & avoid disruptive error UI
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        // If available on the target SDK, enable heap termination on corruption (no-op otherwise)
        HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

        // Install handlers early
        SetUnhandledExceptionFilter(&Unhandled);
        _set_invalid_parameter_handler(&InvalidParamHandler);
        _set_purecall_handler(&PurecallHandler);
        _set_se_translator(&SehTranslator);

        // First-chance diagnostics (non-fatal)
        s_vectoredHandler = AddVectoredExceptionHandler(1 /* call first */, &FirstChanceVEH);

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

    std::scoped_lock lock(s_crashLogMutex);
    if (s_crashLog.is_open()) s_crashLog.close();
}

void LogLine(const std::wstring& line) {
    LogLineInternal(line);
}

} // namespace winqol
