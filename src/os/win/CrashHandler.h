#pragma once
// CrashHandler.h — Windows-only, header-only crash handler for Colony-Game
// Drop-in: include in your Win launcher and call win::CrashHandler::Install(...).
//
// Features:
//  - Configurable MiniDumpWriteDump with rich flags (full memory info, thread info, handle data, unloaded modules).
//  - Plain-text report (.txt) alongside the .dmp with OS/CPU/memory/module list + custom metadata.
//  - Hooks CRT & signals: purecall, invalid parameter, new/terminate, SIGSEGV/SIGABRT, etc.
//  - Optional Windows Event Log entry + user MessageBox.
//  - Custom filename pattern with {app},{ver},{build},{pid},{tid},{date},{time}.
//  - ExtraFiles copy (e.g., last session log), live log provider callback, pre/post callbacks.
//  - Manual WriteDumpNow(reason) and TestCrash() helpers.
//  - Re-entrancy guard, debugger detection skip.
//
// License: MIT (adapt/trim to match your project’s license)
//
// Usage (in your Win entry point):
//   #include "os/win/CrashHandler.h"
//   int WINAPI wWinMain(...) {
//       win::CrashHandler::Config cfg;
//       cfg.app_name = L"Colony-Game";
//       cfg.version  = L"1.0.0";
//       cfg.build_id = L"git:abcdef";
//       cfg.dumps_dir = L"crashdumps";
//       cfg.show_message_box = true;
//       cfg.write_event_log = false; // opt-in
//       win::CrashHandler::Install(cfg);
//       return RunGame(...);
//   }

#if !defined(_WIN32)
#  error "CrashHandler.h is Windows-only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

// Windows & CRT
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <crtdbg.h>
#include <new>
#include <csignal>

// C/C++
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstdio>
#include <algorithm>

// Link required libs (Visual Studio/MSVC)
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Advapi32.lib")

namespace win {

// -------------------------------
// Utility: narrow/wide conversions
// -------------------------------
inline std::string WToUTF8(const std::wstring& w) {
    if (w.empty()) return {};
    int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), bytes, nullptr, nullptr);
    return s;
}

inline std::wstring UTF8ToW(const std::string& s) {
    if (s.empty()) return {};
    int chars = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(chars, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), chars);
    return w;
}

// -------------------------------
// Utility: paths & time
// -------------------------------
inline std::filesystem::path ExePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, buf + len));
}
inline std::filesystem::path ExeDir() { return ExePath().parent_path(); }

inline std::wstring NowDateYYYYMMDD() {
    SYSTEMTIME st{}; ::GetLocalTime(&st);
    wchar_t buf[16];
    swprintf(buf, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}
inline std::wstring NowTimeHHMMSS() {
    SYSTEMTIME st{}; ::GetLocalTime(&st);
    wchar_t buf[16];
    swprintf(buf, 16, L"%02u%02u%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// -------------------------------
// OS / CPU / Memory info helpers
// -------------------------------
struct MemStatus {
    ULONGLONG totalPhys = 0, availPhys = 0;
    ULONGLONG totalPage = 0, availPage = 0;
    ULONGLONG totalVirt = 0, availVirt = 0;
};
inline MemStatus GetMemStatus() {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    ::GlobalMemoryStatusEx(&ms);
    MemStatus r;
    r.totalPhys = ms.ullTotalPhys;   r.availPhys = ms.ullAvailPhys;
    r.totalPage = ms.ullTotalPageFile; r.availPage = ms.ullAvailPageFile;
    r.totalVirt = ms.ullTotalVirtual;  r.availVirt = ms.ullAvailVirtual;
    return r;
}

// Minimal RtlGetVersion signature (avoid winternl.h dependency)
typedef LONG (WINAPI* RtlGetVersionPtr)(void*);
struct RTL_OSVERSIONINFOW_MIN {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};
inline std::wstring OSVersionString() {
    std::wstring out = L"Windows (unknown)";
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(::GetProcAddress(ntdll, "RtlGetVersion"));
        RTL_OSVERSIONINFOW_MIN vi{};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (fn && fn(&vi) == 0 /* STATUS_SUCCESS */) {
            std::wstringstream ss;
            ss << L"Windows " << vi.dwMajorVersion << L"." << vi.dwMinorVersion
               << L" (build " << vi.dwBuildNumber << L")";
            if (vi.szCSDVersion[0]) ss << L" " << vi.szCSDVersion;
            out = ss.str();
        }
    }
    SYSTEM_INFO si{}; ::GetNativeSystemInfo(&si);
    std::wstringstream ss;
    ss << out << L", arch=";
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: ss << L"x64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: ss << L"ARM64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: ss << L"x86"; break;
        default: ss << L"unknown"; break;
    }
    return ss.str();
}

#if defined(_MSC_VER)
#  include <intrin.h>
inline std::wstring CPUBrand() {
    int cpuInfo[4] = {0};
    char brand[0x40] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned nExIds = cpuInfo[0];
    if (nExIds >= 0x80000004) {
        __cpuid((int*)cpuInfo, 0x80000002); memcpy(brand, cpuInfo, sizeof(cpuInfo));
        __cpuid((int*)cpuInfo, 0x80000003); memcpy(brand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid((int*)cpuInfo, 0x80000004); memcpy(brand + 32, cpuInfo, sizeof(cpuInfo));
    }
    std::string s(brand);
    // trim
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){return !std::isspace(c);} ));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){return !std::isspace(c);} ).base(), s.end());
    return UTF8ToW(s);
}
#else
inline std::wstring CPUBrand() { return L"(unknown)"; }
#endif

// -------------------------------
// Module enumeration & version
// -------------------------------
inline std::wstring FileVersionString(const std::wstring& path) {
    DWORD dummy = 0;
    DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return L"";
    std::vector<BYTE> buf(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, buf.data())) return L"";

    VS_FIXEDFILEINFO* ffi = nullptr; UINT ffiLen = 0;
    if (::VerQueryValueW(buf.data(), L"\\", (LPVOID*)&ffi, &ffiLen) && ffiLen >= sizeof(VS_FIXEDFILEINFO)) {
        std::wstringstream ss;
        ss << HIWORD(ffi->dwFileVersionMS) << L"."
           << LOWORD(ffi->dwFileVersionMS) << L"."
           << HIWORD(ffi->dwFileVersionLS) << L"."
           << LOWORD(ffi->dwFileVersionLS);
        return ss.str();
    }
    return L"";
}

struct ModuleInfo {
    std::wstring path;
    void*        base = nullptr;
    DWORD        size = 0;
    std::wstring version;
};

inline std::vector<ModuleInfo> EnumerateModules(DWORD pid) {
    std::vector<ModuleInfo> mods;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return mods;

    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    if (::Module32FirstW(snap, &me)) {
        do {
            ModuleInfo m;
            m.path = me.szExePath;
            m.base = me.modBaseAddr;
            m.size = me.modBaseSize;
            m.version = FileVersionString(m.path);
            mods.push_back(std::move(m));
        } while (::Module32NextW(snap, &me));
    }
    ::CloseHandle(snap);
    return mods;
}

// -------------------------------
// Crash Handler
// -------------------------------
class CrashHandler {
public:
    using BeforeDumpCallback = std::function<void()>;
    using AfterDumpCallback  = std::function<void(const std::wstring& dumpPath,
                                                  const std::wstring& reportPath)>;
    using LogProvider        = std::function<std::wstring()>;

    struct Config {
        std::wstring app_name = L"App";
        std::wstring version  = L"0.0.0";
        std::wstring build_id;                 // e.g., "git:abcdef"
        std::wstring dumps_dir = L"crashdumps"; // relative -> under exe dir
        std::wstring file_pattern = L"{app}_{ver}_{date}-{time}_{pid}_{tid}"; // no extension
        // Minidump flags (sensible rich default):
        MINIDUMP_TYPE dump_type =
            (MINIDUMP_TYPE)(
               MiniDumpWithDataSegs |
               MiniDumpWithPrivateReadWriteMemory |
               MiniDumpWithHandleData |
               MiniDumpWithFullMemoryInfo |
               MiniDumpWithThreadInfo |
               MiniDumpWithUnloadedModules |
               MiniDumpScanMemory
            );
        bool also_write_report_txt = true;
        bool show_message_box      = true;
        bool write_event_log       = false;
        bool skip_if_debugger_present = true; // don't steal exceptions while debugging
        bool install_crt_handlers  = true;
        bool install_signal_handlers = true;
        bool install_vectored_first_chance = false; // write dumps on first-chance (advanced/noisy)
        // User extensibility:
        std::vector<std::filesystem::path> extra_files_to_copy; // e.g., "logs/last_run.log"
        std::unordered_map<std::wstring, std::wstring> metadata; // arbitrary key -> value
        BeforeDumpCallback on_before_dump;
        AfterDumpCallback  on_after_dump;
        LogProvider        live_log_provider;
    };

    // Install/uninstall the crash handler for the process.
    static void Install(const Config& cfg) {
        std::lock_guard<std::mutex> lock(detail().mtx);
        if (detail().installed) return;
        detail().cfg = cfg;

        // Resolve dumps dir (relative => under exe dir)
        detail().dumps_dir = ResolvePath(cfg.dumps_dir);
        std::error_code ec; std::filesystem::create_directories(detail().dumps_dir, ec);

        // Silence WER dialogs
        ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

        // Save previous handlers and install ours
        detail().prev_uef = ::SetUnhandledExceptionFilter(&UnhandledExceptionFilterThunk);

        if (cfg.install_crt_handlers) {
            _set_purecall_handler(&PurecallHandlerThunk);
            _set_invalid_parameter_handler(&InvalidParameterHandlerThunk);
            std::set_terminate(&TerminateHandlerThunk);
            std::set_new_handler(&NewHandlerThunk);
        }
        if (cfg.install_signal_handlers) {
            std::signal(SIGABRT, &SignalHandlerThunk);
            std::signal(SIGFPE,  &SignalHandlerThunk);
            std::signal(SIGILL,  &SignalHandlerThunk);
            std::signal(SIGINT,  &SignalHandlerThunk);
            std::signal(SIGSEGV, &SignalHandlerThunk);
            std::signal(SIGTERM, &SignalHandlerThunk);
        }
        if (cfg.install_vectored_first_chance) {
            // priority=1 => first handler in chain
            detail().veh_cookie = ::AddVectoredExceptionHandler(1, &VectoredHandlerThunk);
        }

        detail().installed = true;
    }

    static void Uninstall() {
        std::lock_guard<std::mutex> lock(detail().mtx);
        if (!detail().installed) return;
        if (detail().veh_cookie) {
            ::RemoveVectoredExceptionHandler(detail().veh_cookie);
            detail().veh_cookie = nullptr;
        }
        ::SetUnhandledExceptionFilter(detail().prev_uef);
        detail().prev_uef = nullptr;
        detail().installed = false;
    }

    // Manual dump on demand (e.g., from an assert path). Returns true on success.
    static bool WriteDumpNow(const std::wstring& reason = L"Manual") {
        if (detail().cfg.skip_if_debugger_present && ::IsDebuggerPresent())
            return false;
        return WriteDumpInternal(/*exc*/nullptr, reason.c_str(), /*firstChance*/false);
    }

    // Add/update metadata key-value (appears in report.txt)
    static void SetMetadata(const std::wstring& key, const std::wstring& value) {
        std::lock_guard<std::mutex> lock(detail().mtx);
        detail().cfg.metadata[key] = value;
    }

    // Replace or add extra file to copy next to the dump (e.g., your log file).
    static void AddExtraFile(const std::filesystem::path& p) {
        std::lock_guard<std::mutex> lock(detail().mtx);
        detail().cfg.extra_files_to_copy.push_back(p);
    }

    // Set/replace live log provider (returns a string appended to the report).
    static void SetLiveLogProvider(LogProvider prov) {
        std::lock_guard<std::mutex> lock(detail().mtx);
        detail().cfg.live_log_provider = std::move(prov);
    }

    // Simple way to validate your pipeline: this will crash by writing to null.
    [[noreturn]] static void TestCrash() {
        volatile int* p = nullptr;
        *p = 42; // boom
        std::abort(); // never reached
    }

private:
    // -------------------------------
    // Internal state (singleton-ish)
    // -------------------------------
    struct Detail {
        std::mutex mtx;
        std::atomic<bool> in_handler { false };
        bool installed = false;
        PVOID veh_cookie = nullptr;
        LPTOP_LEVEL_EXCEPTION_FILTER prev_uef = nullptr;

        Config cfg;
        std::filesystem::path dumps_dir;
    };
    static Detail& detail() {
        static Detail d;
        return d;
    }

    // -------------------------------
    // Helpers
    // -------------------------------
    static std::filesystem::path ResolvePath(const std::filesystem::path& p) {
        if (p.is_absolute()) return p;
        return ExeDir() / p;
    }

    static std::wstring ExeNameNoExt() {
        auto name = ExePath().filename().wstring();
        auto pos = name.find_last_of(L'.');
        if (pos != std::wstring::npos) name.erase(pos);
        return name;
    }

    static std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
        if (from.empty()) return s;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    }

    static std::wstring ComposeBaseName(const std::wstring& extNoDot) {
        const auto& cfg = detail().cfg;
        std::wstring base = cfg.file_pattern;
        std::wstring app = cfg.app_name.empty() ? ExeNameNoExt() : cfg.app_name;
        base = ReplaceAll(base, L"{app}",   app);
        base = ReplaceAll(base, L"{ver}",   cfg.version);
        base = ReplaceAll(base, L"{build}", cfg.build_id);
        base = ReplaceAll(base, L"{pid}",   std::to_wstring(::GetCurrentProcessId()));
        base = ReplaceAll(base, L"{tid}",   std::to_wstring(::GetCurrentThreadId()));
        base = ReplaceAll(base, L"{date}",  NowDateYYYYMMDD());
        base = ReplaceAll(base, L"{time}",  NowTimeHHMMSS());
        if (!extNoDot.empty()) {
            base += L"." + extNoDot;
        }
        return base;
    }

    static std::filesystem::path MakeDumpPath() {
        return detail().dumps_dir / ComposeBaseName(L"dmp");
    }
    static std::filesystem::path MakeReportPath() {
        return detail().dumps_dir / ComposeBaseName(L"txt");
    }

    static void EnsureDirs() {
        std::error_code ec; std::filesystem::create_directories(detail().dumps_dir, ec);
    }

    static bool IsFirstInHandler() {
        bool expected = false;
        return detail().in_handler.compare_exchange_strong(expected, true);
    }
    static void LeaveHandler() {
        detail().in_handler.store(false);
    }

    static void SafeMessageBox(const std::wstring& msg, const std::wstring& title) {
        // Avoid reentrancy if user spams OK; not a big deal.
        ::MessageBoxW(nullptr, msg.c_str(), title.c_str(),
                      MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }

    static void MaybeWriteEventLog(const std::wstring& summary) {
        if (!detail().cfg.write_event_log) return;
        HANDLE h = ::RegisterEventSourceW(nullptr, detail().cfg.app_name.c_str());
        if (!h) return;
        LPCWSTR strs[1] = { summary.c_str() };
        ::ReportEventW(h,
            EVENTLOG_ERROR_TYPE, /*eventType*/
            0 /*category*/, 0 /*eventId*/,
            nullptr /*sid*/, 1 /*numStrings*/,
            0 /*dataSize*/, strs, nullptr /*rawData*/);
        ::DeregisterEventSource(h);
    }

    // -----------------------------------------
    // Core: write the minidump + text report
    // -----------------------------------------
    static bool WriteDumpInternal(EXCEPTION_POINTERS* exc,
                                  const wchar_t* reason,
                                  bool firstChance)
    {
        EnsureDirs();

        const auto dumpPath   = MakeDumpPath();
        const auto reportPath = detail().cfg.also_write_report_txt ? MakeReportPath()
                                                                   : std::filesystem::path();

        // Pre-callback (keep it light!)
        if (detail().cfg.on_before_dump) {
            try { detail().cfg.on_before_dump(); } catch (...) {}
        }

        // Create dump file
        HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                     nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = false;
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = ::GetCurrentThreadId();
            mei.ExceptionPointers = exc;
            mei.ClientPointers = FALSE;

            // Add extended info
            MINIDUMP_TYPE mtype = detail().cfg.dump_type;

            // (optionally add comment stream / user stream)
            std::vector<MINIDUMP_USER_STREAM> userStreams;
            std::vector<wchar_t> reasonBuf;
            if (reason && *reason) {
                // NOTE: MINIDUMP_USER_STREAM expects bytes; we'll store UTF-16LE text
                const size_t n = wcslen(reason);
                reasonBuf.resize(n + 1);
                memcpy(reasonBuf.data(), reason, (n + 1) * sizeof(wchar_t));
                MINIDUMP_USER_STREAM s{};
                s.Type = CommentStreamW; // "reason" string
                s.Buffer = reasonBuf.data();
                s.BufferSize = (ULONG)((n + 1) * sizeof(wchar_t));
                userStreams.push_back(s);
            }
            MINIDUMP_USER_STREAM_INFORMATION usi{};
            if (!userStreams.empty()) {
                usi.UserStreamCount = (ULONG)userStreams.size();
                usi.UserStreamArray = userStreams.data();
            }

            ok = !!::MiniDumpWriteDump(
                ::GetCurrentProcess(),
                ::GetCurrentProcessId(),
                hFile,
                mtype,
                exc ? &mei : nullptr,
                userStreams.empty() ? nullptr : &usi,
                nullptr);

            ::CloseHandle(hFile);
        }

        // Write report text
        if (detail().cfg.also_write_report_txt) {
            WriteTextReport(reportPath, dumpPath, exc, reason, firstChance, ok);
        }

        // Copy extra files
        for (const auto& p : detail().cfg.extra_files_to_copy) {
            std::error_code ec;
            const auto src = ResolvePath(p);
            if (std::filesystem::exists(src, ec)) {
                auto dst = detail().dumps_dir / src.filename();
                // Avoid clobbering: append time if target exists
                if (std::filesystem::exists(dst, ec)) {
                    auto stem = dst.stem().wstring();
                    auto ext  = dst.extension().wstring();
                    dst = detail().dumps_dir / (stem + L"_" + NowDateYYYYMMDD() + L"-" + NowTimeHHMMSS() + ext);
                }
                std::filesystem::copy_file(src, dst,
                    std::filesystem::copy_options::overwrite_existing, ec);
                (void)ec;
            }
        }

        // Post callback
        if (detail().cfg.on_after_dump) {
            try { detail().cfg.on_after_dump(dumpPath.wstring(), reportPath.wstring()); } catch (...) {}
        }

        // UX: Message box (optional)
        if (detail().cfg.show_message_box) {
            std::wstringstream ss;
            ss << detail().cfg.app_name << L" encountered a problem and must close.\n\n";
            if (ok) {
                ss << L"A crash dump was written to:\n" << dumpPath.wstring();
                if (detail().cfg.also_write_report_txt) {
                    ss << L"\n\nReport:\n" << reportPath.wstring();
                }
            } else {
                ss << L"Failed to write a crash dump to:\n" << dumpPath.wstring();
            }
            SafeMessageBox(ss.str(), detail().cfg.app_name);
        }

        // Event log (optional)
        {
            std::wstringstream ss;
            ss << detail().cfg.app_name << L" crash ";
            if (detail().cfg.version.size()) ss << L"v" << detail().cfg.version << L" ";
            if (detail().cfg.build_id.size()) ss << L"(" << detail().cfg.build_id << L") ";
            ss << L"pid=" << ::GetCurrentProcessId()
               << L" tid=" << ::GetCurrentThreadId()
               << L" ok=" << (ok ? L"1" : L"0")
               << L" firstChance=" << (firstChance ? L"1" : L"0");
            MaybeWriteEventLog(ss.str());
        }

        return ok;
    }

    static void WriteTextReport(const std::filesystem::path& reportPath,
                                const std::filesystem::path& dumpPath,
                                EXCEPTION_POINTERS* exc,
                                const wchar_t* reason,
                                bool firstChance,
                                bool dumpOK)
    {
        std::ofstream out(reportPath, std::ios::binary);
        if (!out) return;

        auto writeUTF8 = [&](const std::wstring& w) {
            const auto s = WToUTF8(w);
            out.write(s.data(), (std::streamsize)s.size());
        };
        auto writeln = [&](const std::wstring& w = L"") {
            writeUTF8(w); out.write("\r\n", 2);
        };

        // Header
        writeln(L"==== Colony-Game Crash Report ====");
        writeln(L"App: " + (detail().cfg.app_name.empty()? ExeNameNoExt() : detail().cfg.app_name));
        if (!detail().cfg.version.empty()) writeln(L"Version: " + detail().cfg.version);
        if (!detail().cfg.build_id.empty()) writeln(L"Build: " + detail().cfg.build_id);
        writeln(L"Date: " + NowDateYYYYMMDD() + L" " + NowTimeHHMMSS());
        writeln(L"PID: " + std::to_wstring(::GetCurrentProcessId()) +
                L"  TID: " + std::to_wstring(::GetCurrentThreadId()));
        writeln(L"Dump file: " + dumpPath.wstring());
        writeln(L"First-chance: " + std::wstring(firstChance? L"yes" : L"no"));
        writeln(L"Dump write: " + std::wstring(dumpOK? L"success" : L"FAILED"));
        writeln();

        // Reason / exception
        if (reason && *reason) {
            writeln(L"Reason: " + std::wstring(reason));
        }
        if (exc && exc->ExceptionRecord) {
            auto code = exc->ExceptionRecord->ExceptionCode;
            std::wstringstream ss;
            ss << L"Exception: 0x" << std::hex << std::uppercase << code
               << L" at 0x" << exc->ExceptionRecord->ExceptionAddress;
            writeln(ss.str());
        }
        DWORD lastErr = ::GetLastError();
        if (lastErr) {
            std::wstringstream ss;
            ss << L"LastError: 0x" << std::hex << std::uppercase << lastErr
               << L" (" << std::dec << lastErr << L")";
            writeln(ss.str());
        }
        writeln();

        // System
        writeln(L"== System ==");
        writeln(L"OS : " + OSVersionString());
        writeln(L"CPU: " + CPUBrand());
        auto ms = GetMemStatus();
        auto fmtBytes = [](ULONGLONG v)->std::wstring {
            std::wstringstream ss; ss << v << L" bytes";
            return ss.str();
        };
        writeln(L"RAM total : " + fmtBytes(ms.totalPhys) + L", avail: " + fmtBytes(ms.availPhys));
        writeln(L"Page total: " + fmtBytes(ms.totalPage) + L", avail: " + fmtBytes(ms.availPage));
        writeln(L"Virt total: " + fmtBytes(ms.totalVirt) + L", avail: " + fmtBytes(ms.availVirt));
        writeln();

        // Metadata
        if (!detail().cfg.metadata.empty()) {
            writeln(L"== Metadata ==");
            for (const auto& kv : detail().cfg.metadata) {
                writeln(kv.first + L": " + kv.second);
            }
            writeln();
        }

        // Live log provider
        if (detail().cfg.live_log_provider) {
            std::wstring log;
            try { log = detail().cfg.live_log_provider(); } catch (...) {}
            if (!log.empty()) {
                writeln(L"== Live Log ==");
                writeln(log);
                writeln();
            }
        }

        // Modules
        writeln(L"== Modules ==");
        auto mods = EnumerateModules(::GetCurrentProcessId());
        for (const auto& m : mods) {
            std::wstringstream ss;
            ss << L"* " << m.path
               << L" [base=0x" << std::hex << (uintptr_t)m.base
               << L" size=0x" << (unsigned)m.size << std::dec << L"]";
            if (!m.version.empty()) ss << L" v" << m.version;
            writeln(ss.str());
        }

        out.flush();
    }

    // -------------------------------
    // Handlers (SEH / CRT / Signals)
    // -------------------------------
    static LONG WINAPI UnhandledExceptionFilterThunk(EXCEPTION_POINTERS* info) {
        if (detail().cfg.skip_if_debugger_present && ::IsDebuggerPresent()) {
            // Chain to previous handler/debugger
            if (detail().prev_uef) return detail().prev_uef(info);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (!IsFirstInHandler()) {
            // Already handling a crash. Let default proceed to avoid recursion.
            return EXCEPTION_CONTINUE_SEARCH;
        }
        WriteDumpInternal(info, L"UnhandledExceptionFilter", /*firstChance*/false);
        LeaveHandler();
        return EXCEPTION_EXECUTE_HANDLER; // terminate after handling
    }

    static LONG CALLBACK VectoredHandlerThunk(PEXCEPTION_POINTERS info) {
        // This fires on first-chance exceptions if enabled.
        if (!detail().cfg.install_vectored_first_chance) return EXCEPTION_CONTINUE_SEARCH;
        if (detail().cfg.skip_if_debugger_present && ::IsDebuggerPresent())
            return EXCEPTION_CONTINUE_SEARCH;

        // Only write for serious exceptions to avoid noise (access violation, stack overflow, etc.)
        DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_STACK_OVERFLOW:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_PRIV_INSTRUCTION:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_INVALID_DISPOSITION:
                // Try non-reentrant single dump
                if (IsFirstInHandler()) {
                    WriteDumpInternal(info, L"VectoredFirstChance", /*firstChance*/true);
                    LeaveHandler();
                }
                break;
            default: break;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // CRT / C++ runtime hooks:
    static void __cdecl PurecallHandlerThunk() {
        if (IsFirstInHandler()) {
            WriteDumpInternal(nullptr, L"Pure virtual function call", /*firstChance*/false);
            LeaveHandler();
        }
        std::abort();
    }
    static void __cdecl InvalidParameterHandlerThunk(const wchar_t* expr,
                                                     const wchar_t* func,
                                                     const wchar_t* file,
                                                     unsigned line,
                                                     uintptr_t /*pReserved*/) {
        std::wstringstream ss;
        ss << L"Invalid parameter: " << (expr ? expr : L"(null)")
           << L" in " << (func ? func : L"(func)")
           << L" at " << (file ? file : L"(file)")
           << L":" << line;
        if (IsFirstInHandler()) {
            WriteDumpInternal(nullptr, ss.str().c_str(), /*firstChance*/false);
            LeaveHandler();
        }
        std::abort();
    }
    [[noreturn]] static void __cdecl TerminateHandlerThunk() {
        if (IsFirstInHandler()) {
            WriteDumpInternal(nullptr, L"std::terminate", /*firstChance*/false);
            LeaveHandler();
        }
        std::abort();
    }
    static void __cdecl NewHandlerThunk() {
        if (IsFirstInHandler()) {
            WriteDumpInternal(nullptr, L"std::new bad_alloc", /*firstChance*/false);
            LeaveHandler();
        }
        std::abort();
    }

    // Signals
    static void __cdecl SignalHandlerThunk(int sig) {
        const wchar_t* why = L"Signal";
        switch (sig) {
            case SIGABRT: why = L"SIGABRT"; break;
            case SIGFPE : why = L"SIGFPE";  break;
            case SIGILL : why = L"SIGILL";  break;
            case SIGINT : why = L"SIGINT";  break;
            case SIGSEGV: why = L"SIGSEGV"; break;
            case SIGTERM: why = L"SIGTERM"; break;
            default: break;
        }
        if (IsFirstInHandler()) {
            WriteDumpInternal(nullptr, why, /*firstChance*/false);
            LeaveHandler();
        }
        std::abort();
    }
};

} // namespace win
