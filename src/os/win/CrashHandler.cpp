// src/os/win/CrashHandler.cpp
//
// Massive, production-grade expansion of the simple crash handler.
// Works with your existing minimal header:
//
//   // CrashHandler.h
//   #pragma once
//   #ifdef _WIN32
//   #  include <windows.h>
//   namespace win {
//     void InstallCrashHandler(const wchar_t* dumpsDir = L"crashdumps");
//     LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* info);
//   }
//   #endif
//
// Features in this .cpp:
//  • Rich minidumps (MiniDumpWriteDump with extended flags)
//  • Sidecar UTF‑8 text report with OS/CPU/memory/exception/stack/modules
//  • Symbol resolution (function + file:line) if PDBs are present
//  • Safe re‑entrancy guard (prevents recursive crashes in the handler)
//  • CRT hooks (purecall, invalid parameter, terminate, new) + signal hooks
//  • Optional Windows Event Log entry (compile-time toggle)
//  • Skips dump when a debugger is attached (compile-time toggle)
//  • Customizable file naming via environment variable CRASH_FILE_PATTERN
//  • Optional extra file copying via CRASH_EXTRA_FILES (semicolon‑separated)
//
// Build: MSVC, C++17. Link: Dbghelp.lib, Version.lib, Advapi32.lib
//
// You can tune defaults with these (compile-time) defines before you compile:
//   CRASH_SHOW_MESSAGEBOX=0/1        (default 1)
//   CRASH_SKIP_IF_DEBUGGER=0/1       (default 1)
//   CRASH_WRITE_EVENTLOG=0/1         (default 0)
//   CRASH_ENABLE_VECTORED_FIRST=0/1  (default 0; writes first-chance dumps for severe exceptions)
//
// You can also set at runtime (environment variables):
//   CRASH_FILE_PATTERN   default: "{app}_{date}-{time}_{pid}_{tid}" (no extension)
//   CRASH_BUILD_ID       optional build id (e.g., git hash) in report and filename placeholder {build}
//   CRASH_EXTRA_FILES    e.g.: "logs\\last_run.log;settings.json"
//
// File name placeholders for CRASH_FILE_PATTERN:
//   {app} {ver} {build} {pid} {tid} {date} {time}
//
// NOTE: The header’s InstallCrashHandler(const wchar_t* dumpsDir) is unchanged.
//       All additional behavior is internal to this .cpp.
//

#ifdef _WIN32

#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "CrashHandler.h"
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <crtdbg.h>
#include <new>
#include <csignal>

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cstdint>

// --- Link Libraries ---
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Psapi.lib")

// --- Defaults/Toggles ---
#ifndef CRASH_SHOW_MESSAGEBOX
#  define CRASH_SHOW_MESSAGEBOX 1
#endif
#ifndef CRASH_SKIP_IF_DEBUGGER
#  define CRASH_SKIP_IF_DEBUGGER 1
#endif
#ifndef CRASH_WRITE_EVENTLOG
#  define CRASH_WRITE_EVENTLOG 0
#endif
#ifndef CRASH_ENABLE_VECTORED_FIRST
#  define CRASH_ENABLE_VECTORED_FIRST 0
#endif

// Rich minidump default:
#ifndef CRASH_DUMP_TYPE
#  define CRASH_DUMP_TYPE (MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory | MiniDumpWithHandleData | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpScanMemory)
#endif

// -----------------------------------------------------------------------------
// Small utilities (wide/utf8, time, paths)
// -----------------------------------------------------------------------------
namespace {

std::string WToUTF8(const std::wstring& w) {
    if (w.empty()) return {};
    int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), bytes, nullptr, nullptr);
    return s;
}

std::wstring UTF8ToW(const std::string& s) {
    if (s.empty()) return {};
    int chars = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(chars, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), chars);
    return w;
}

std::filesystem::path ExePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, buf + len));
}
std::filesystem::path ExeDir() { return ExePath().parent_path(); }
std::wstring ExeNameNoExt() {
    auto s = ExePath().filename().wstring();
    auto dot = s.find_last_of(L'.');
    if (dot != std::wstring::npos) s.erase(dot);
    return s;
}

std::wstring NowDateYYYYMMDD() {
    SYSTEMTIME st{}; ::GetLocalTime(&st);
    wchar_t buf[16];
    swprintf(buf, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}
std::wstring NowTimeHHMMSS() {
    SYSTEMTIME st{}; ::GetLocalTime(&st);
    wchar_t buf[16];
    swprintf(buf, 16, L"%02u%02u%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::wstring ReadEnvW(const wchar_t* name) {
    DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return L"";
    std::wstring val; val.resize(n);
    DWORD got = ::GetEnvironmentVariableW(name, val.data(), n);
    if (got && got < n) val.resize(got);
    return val;
}

} // anon

// -----------------------------------------------------------------------------
// System info helpers (OS/CPU/memory/version/modules)
// -----------------------------------------------------------------------------
namespace {

typedef LONG (WINAPI* RtlGetVersionPtr)(void*);
struct RTL_OSVERSIONINFOW_MIN {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};

std::wstring OSVersionString() {
    std::wstring out = L"Windows (unknown)";
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(::GetProcAddress(ntdll, "RtlGetVersion"));
        RTL_OSVERSIONINFOW_MIN vi{};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (fn && fn(&vi) == 0) {
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
std::wstring CPUBrand() {
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
std::wstring CPUBrand() { return L"(unknown)"; }
#endif

struct MemStatus {
    ULONGLONG totalPhys = 0, availPhys = 0;
    ULONGLONG totalPage = 0, availPage = 0;
    ULONGLONG totalVirt = 0, availVirt = 0;
};
MemStatus GetMemStatus() {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    ::GlobalMemoryStatusEx(&ms);
    MemStatus r;
    r.totalPhys = ms.ullTotalPhys;     r.availPhys = ms.ullAvailPhys;
    r.totalPage = ms.ullTotalPageFile; r.availPage = ms.ullAvailPageFile;
    r.totalVirt = ms.ullTotalVirtual;  r.availVirt = ms.ullAvailVirtual;
    return r;
}

std::wstring FileVersionString(const std::wstring& path) {
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

std::vector<ModuleInfo> EnumerateModules(DWORD pid) {
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
    std::sort(mods.begin(), mods.end(), [](const ModuleInfo& a, const ModuleInfo& b) {
        return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
    });
    return mods;
}

std::wstring BytesToString(ULONGLONG v) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int ui = 0;
    long double d = static_cast<long double>(v);
    while (d >= 1024.0 && ui < 4) { d /= 1024.0; ++ui; }
    std::wstringstream ss; ss.setf(std::ios::fixed);
    ss << std::setprecision((ui==0)?0:2) << d << L" " << units[ui];
    return ss.str();
}

} // anon

// -----------------------------------------------------------------------------
// Symbol/stack helpers
// -----------------------------------------------------------------------------
namespace {

struct SymInitRAII {
    HANDLE hProcess = GetCurrentProcess();
    bool   ok = false;
    SymInitRAII() {
        DWORD opts = SymGetOptions();
        opts |= SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES;
        SymSetOptions(opts);
        // Use env paths (_NT_SYMBOL_PATH) plus local dir
        std::wstring search = ExeDir().wstring();
        ok = !!SymInitializeW(hProcess, search.c_str(), TRUE);
    }
    ~SymInitRAII() {
        if (ok) SymCleanup(hProcess);
    }
};

std::wstring AddressToSymbolString(DWORD64 addr) {
    std::wstringstream out;

    // Allocate SYMBOL_INFO with room for name
    BYTE buffer[sizeof(SYMBOL_INFOW) + 512*sizeof(wchar_t)]{};
    auto sym = reinterpret_cast<PSYMBOL_INFOW>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
    sym->MaxNameLen = 512;

    DWORD64 disp = 0;
    if (SymFromAddrW(GetCurrentProcess(), addr, &disp, sym)) {
        out << sym->Name << L"+0x" << std::hex << disp;
    } else {
        out << L"(unknown)";
    }

    // Try file:line
    DWORD dispLine = 0;
    IMAGEHLP_LINEW64 line{}; line.SizeOfStruct = sizeof(line);
    if (SymGetLineFromAddrW64(GetCurrentProcess(), addr, &dispLine, &line)) {
        out << L" (" << line.FileName << L":" << std::dec << line.LineNumber << L")";
    }
    return out.str();
}

void WriteStack(std::ofstream& out, EXCEPTION_POINTERS* info) {
    auto write = [&](const std::wstring& w) { auto s=WToUTF8(w); out.write(s.data(), (std::streamsize)s.size()); };
    auto writeln = [&](const std::wstring& w=L""){ write(w); out.write("\r\n", 2); };

    CONTEXT ctx{};
    if (info && info->ContextRecord) {
        ctx = *info->ContextRecord;
    } else {
#if defined(_M_X64) || defined(_M_ARM64) || defined(_M_IX86)
        RtlCaptureContext(&ctx);
#endif
    }

#if defined(_M_IX86)
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Eip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp; frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_X64)
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
    DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Pc;  frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Fp;  frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Sp;  frame.AddrStack.Mode = AddrModeFlat;
#else
    writeln(L"(Stack walking not supported on this architecture)");
    return;
#endif

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    SymInitRAII sym; // initializes sym subsystem
    writeln(L"== Stack Trace ==");
    for (int i = 0; i < 128; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;

        std::wstringstream one;
        one << L"  #" << i << L"  0x" << std::hex << frame.AddrPC.Offset << L"  "
            << AddressToSymbolString(frame.AddrPC.Offset);
        writeln(one.str());
    }
    writeln();
}

} // anon

// -----------------------------------------------------------------------------
// Crash core (state, dump writing, report writing, event log, helpers)
// -----------------------------------------------------------------------------
namespace {

struct State {
    std::mutex mtx;
    std::atomic<bool> in_handler{false};
    std::filesystem::path dumps_dir;
    std::wstring app_name;
    std::wstring app_version;
    std::wstring build_id; // from env CRASH_BUILD_ID
    LPTOP_LEVEL_EXCEPTION_FILTER prev_uef = nullptr;
    PVOID vectored_cookie = nullptr;
} g;

// Compose base filename (no extension) using env pattern or default.
std::wstring ComposeBaseName() {
    std::wstring pattern = ReadEnvW(L"CRASH_FILE_PATTERN");
    if (pattern.empty()) pattern = L"{app}_{date}-{time}_{pid}_{tid}";

    if (g.app_name.empty()) g.app_name = ExeNameNoExt();

    auto base = pattern;
    base = ReplaceAll(base, L"{app}",   g.app_name);
    base = ReplaceAll(base, L"{ver}",   g.app_version);
    base = ReplaceAll(base, L"{build}", g.build_id);
    base = ReplaceAll(base, L"{pid}",   std::to_wstring(::GetCurrentProcessId()));
    base = ReplaceAll(base, L"{tid}",   std::to_wstring(::GetCurrentThreadId()));
    base = ReplaceAll(base, L"{date}",  NowDateYYYYMMDD());
    base = ReplaceAll(base, L"{time}",  NowTimeHHMMSS());
    return base;
}

std::filesystem::path DumpPath()   { return g.dumps_dir / (ComposeBaseName() + L".dmp"); }
std::filesystem::path ReportPath() { return g.dumps_dir / (ComposeBaseName() + L".txt"); }

void EnsureDumpDir() {
    std::error_code ec; std::filesystem::create_directories(g.dumps_dir, ec);
    (void)ec;
}

std::wstring ExceptionCodeToString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return L"EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return L"EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return L"EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return L"EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return L"EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return L"EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return L"EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return L"EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return L"EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return L"EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return L"EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return L"EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return L"EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return L"EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return L"EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return L"EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return L"EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return L"EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return L"EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return L"EXCEPTION_STACK_OVERFLOW";
        default: {
            std::wstringstream ss; ss << L"0x" << std::hex << std::uppercase << code;
            return ss.str();
        }
    }
}

void SafeMessageBox(const std::wstring& msg, const std::wstring& title) {
#if CRASH_SHOW_MESSAGEBOX
    ::MessageBoxW(nullptr, msg.c_str(), title.c_str(),
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
#else
    (void)msg; (void)title;
#endif
}

void MaybeWriteEventLog(const std::wstring& summary) {
#if CRASH_WRITE_EVENTLOG
    HANDLE h = ::RegisterEventSourceW(nullptr, g.app_name.c_str());
    if (!h) return;
    LPCWSTR strs[1] = { summary.c_str() };
    ::ReportEventW(h, EVENTLOG_ERROR_TYPE, 0 /*category*/, 0 /*eventId*/,
                   nullptr /*sid*/, 1 /*numStrings*/, 0 /*dataSize*/, strs, nullptr);
    ::DeregisterEventSource(h);
#else
    (void)summary;
#endif
}

void CopyExtraFiles() {
    std::wstring env = ReadEnvW(L"CRASH_EXTRA_FILES");
    if (env.empty()) return;
    std::wstringstream ss(env);
    std::wstring item;
    while (std::getline(ss, item, L';')) {
        if (item.empty()) continue;
        std::error_code ec;
        std::filesystem::path src = item;
        if (!src.is_absolute()) src = ExeDir() / src;
        if (!std::filesystem::exists(src, ec)) continue;
        auto dst = g.dumps_dir / src.filename();
        if (std::filesystem::exists(dst, ec)) {
            auto stem = dst.stem().wstring();
            auto ext = dst.extension().wstring();
            dst = g.dumps_dir / (stem + L"_" + NowDateYYYYMMDD() + L"-" + NowTimeHHMMSS() + ext);
        }
        std::filesystem::copy_file(src, dst,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
}

void WriteTextReport(const std::filesystem::path& reportPath,
                     const std::filesystem::path& dumpPath,
                     EXCEPTION_POINTERS* exc,
                     const wchar_t* reason,
                     bool firstChance,
                     bool dumpOK)
{
    std::ofstream out(reportPath, std::ios::binary);
    if (!out) return;

    auto write = [&](const std::wstring& w){ auto s=WToUTF8(w); out.write(s.data(), (std::streamsize)s.size()); };
    auto writeln = [&](const std::wstring& w=L""){ write(w); out.write("\r\n", 2); };

    writeln(L"==== Crash Report ====");
    writeln(L"App      : " + (g.app_name.empty()? ExeNameNoExt(): g.app_name));
    if (!g.app_version.empty()) writeln(L"Version  : " + g.app_version);
    if (!g.build_id.empty())    writeln(L"Build    : " + g.build_id);
    writeln(L"Date/Time: " + NowDateYYYYMMDD() + L" " + NowTimeHHMMSS());
    writeln(L"PID/TID  : " + std::to_wstring(::GetCurrentProcessId()) + L"/" + std::to_wstring(::GetCurrentThreadId()));
    writeln(L"Dump file: " + dumpPath.wstring());
    writeln(L"First-chance: " + std::wstring(firstChance? L"yes" : L"no"));
    writeln(L"Dump write  : " + std::wstring(dumpOK? L"success" : L"FAILED"));
    if (reason && *reason) writeln(L"Reason   : " + std::wstring(reason));
    DWORD le = ::GetLastError();
    if (le) { std::wstringstream ss; ss << L"LastError: 0x" << std::hex << std::uppercase << le << L" (" << std::dec << le << L")"; writeln(ss.str()); }
    writeln();

    // Exception info (code/address + some registers)
    if (exc && exc->ExceptionRecord) {
        auto er = exc->ExceptionRecord;
        std::wstringstream ss;
        ss << L"== Exception =="; writeln(ss.str());
        ss.str(L""); ss.clear();
        ss << L"Code   : " << ExceptionCodeToString(er->ExceptionCode)
           << L"  (0x" << std::hex << std::uppercase << er->ExceptionCode << L")";
        writeln(ss.str());
        ss.str(L""); ss.clear();
        ss << L"Address: 0x" << std::hex << std::uppercase << (uintptr_t)er->ExceptionAddress;
        writeln(ss.str());
#if defined(_M_IX86)
        auto& c = *exc->ContextRecord;
        ss.str(L""); ss.clear(); ss << L"EAX=" << std::hex << c.Eax << L" EBX=" << c.Ebx << L" ECX=" << c.Ecx
                                   << L" EDX=" << c.Edx << L" ESI=" << c.Esi << L" EDI=" << c.Edi;
        writeln(ss.str());
        ss.str(L""); ss.clear(); ss << L"EBP=" << c.Ebp << L" ESP=" << c.Esp << L" EIP=" << c.Eip; writeln(ss.str());
#elif defined(_M_X64)
        auto& c = *exc->ContextRecord;
        ss.str(L""); ss.clear();
        ss << L"RAX=" << std::hex << c.Rax << L" RBX=" << c.Rbx << L" RCX=" << c.Rcx
           << L" RDX=" << c.Rdx << L" RSI=" << c.Rsi << L" RDI=" << c.Rdi;
        writeln(ss.str());
        ss.str(L""); ss.clear();
        ss << L"R8 =" << c.R8  << L" R9 =" << c.R9  << L" R10=" << c.R10
           << L" R11=" << c.R11 << L" R12=" << c.R12 << L" R13=" << c.R13
           << L" R14=" << c.R14 << L" R15=" << c.R15; writeln(ss.str());
        ss.str(L""); ss.clear(); ss << L"RBP=" << c.Rbp << L" RSP=" << c.Rsp << L" RIP=" << c.Rip; writeln(ss.str());
#elif defined(_M_ARM64)
        auto& c = *exc->ContextRecord;
        ss.str(L""); ss.clear();
        for (int i=0;i<28;++i){ ss << L"X" << i << L"=" << std::hex << c.X[i] << ((i%4==3)?L"\n":L" "); }
        writeln(ss.str());
        ss.str(L""); ss.clear(); ss << L"FP=" << c.Fp << L" SP=" << c.Sp << L" PC=" << c.Pc; writeln(ss.str());
#endif
        writeln();
    }

    // System info
    writeln(L"== System ==");
    writeln(L"OS : " + OSVersionString());
    writeln(L"CPU: " + CPUBrand());
    auto ms = GetMemStatus();
    writeln(L"RAM total : " + BytesToString(ms.totalPhys) + L", avail: " + BytesToString(ms.availPhys));
    writeln(L"Page total: " + BytesToString(ms.totalPage) + L", avail: " + BytesToString(ms.availPage));
    writeln(L"Virt total: " + BytesToString(ms.totalVirt) + L", avail: " + BytesToString(ms.availVirt));
    writeln();

    // Modules
    writeln(L"== Modules ==");
    for (const auto& m : EnumerateModules(::GetCurrentProcessId())) {
        std::wstringstream ss;
        ss << L"* " << m.path << L" [base=0x" << std::hex << (uintptr_t)m.base
           << L" size=0x" << (unsigned)m.size << std::dec << L"]";
        if (!m.version.empty()) ss << L" v" << m.version;
        writeln(ss.str());
    }
    writeln();

    // Stack
    WriteStack(out, exc);

    out.flush();
}

bool WriteMinidump(EXCEPTION_POINTERS* info, const std::filesystem::path& dumpPath, const wchar_t* reason) {
    HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;

    // Optional user stream: "reason"
    std::vector<MINIDUMP_USER_STREAM> streams;
    std::vector<wchar_t> reasonBuf;
    if (reason && *reason) {
        size_t n = wcslen(reason);
        reasonBuf.resize(n + 1);
        memcpy(reasonBuf.data(), reason, (n + 1)*sizeof(wchar_t));
        MINIDUMP_USER_STREAM s{};
        s.Type = CommentStreamW;
        s.Buffer = reasonBuf.data();
        s.BufferSize = (ULONG)((n + 1) * sizeof(wchar_t));
        streams.push_back(s);
    }
    MINIDUMP_USER_STREAM_INFORMATION usi{};
    if (!streams.empty()) {
        usi.UserStreamCount = (ULONG)streams.size();
        usi.UserStreamArray = streams.data();
    }

    BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(),
                                  ::GetCurrentProcessId(),
                                  hFile,
                                  (MINIDUMP_TYPE)CRASH_DUMP_TYPE,
                                  info ? &mei : nullptr,
                                  streams.empty() ? nullptr : &usi,
                                  nullptr);

    ::CloseHandle(hFile);
    return !!ok;
}

void GatherExeMetadata() {
    g.app_name = ExeNameNoExt();
    g.app_version = FileVersionString(ExePath().wstring());
    g.build_id = ReadEnvW(L"CRASH_BUILD_ID");
}

bool IsDebuggerAttached() {
#if CRASH_SKIP_IF_DEBUGGER
    if (::IsDebuggerPresent()) return true;
    BOOL remote = FALSE;
    ::CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return !!remote;
#else
    return false;
#endif
}

// Re-entrancy guard
bool EnterHandlerOnce() {
    bool expected = false;
    return g.in_handler.compare_exchange_strong(expected, true);
}
void LeaveHandler() {
    g.in_handler.store(false);
}

void ShowUserDialog(const std::filesystem::path& dumpPath,
                    const std::filesystem::path& reportPath,
                    bool dumpOK)
{
#if CRASH_SHOW_MESSAGEBOX
    std::wstringstream ss;
    ss << g.app_name << L" encountered a problem and needs to close.\n\n";
    if (dumpOK) {
        ss << L"A crash dump was written to:\n" << dumpPath.wstring();
        if (!reportPath.empty()) {
            ss << L"\n\nReport:\n" << reportPath.wstring();
        }
    } else {
        ss << L"Failed to write a crash dump to:\n" << dumpPath.wstring();
    }
    ::MessageBoxW(nullptr, ss.str().c_str(), g.app_name.c_str(),
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
#else
    (void)dumpPath; (void)reportPath; (void)dumpOK;
#endif
}

void HandleCrash(EXCEPTION_POINTERS* info, const wchar_t* reason, bool firstChance) {
    // Avoid recursion
    if (!EnterHandlerOnce()) return;

    if (IsDebuggerAttached()) {
        // Let the debugger catch it; do not swallow
        LeaveHandler();
        return;
    }

    EnsureDumpDir();

    const auto dumpPath   = DumpPath();
    const auto reportPath = ReportPath();

    const bool ok = WriteMinidump(info, dumpPath, reason);
    WriteTextReport(reportPath, dumpPath, info, reason, firstChance, ok);
    CopyExtraFiles();

    // Event Log summary
    {
        std::wstringstream ev;
        ev << g.app_name << L" crash";
        if (!g.app_version.empty()) ev << L" v" << g.app_version;
        if (!g.build_id.empty())    ev << L" (" << g.build_id << L")";
        ev << L" pid=" << ::GetCurrentProcessId()
           << L" tid=" << ::GetCurrentThreadId()
           << L" ok=" << (ok?L"1":L"0")
           << L" firstChance=" << (firstChance?L"1":L"0");
        MaybeWriteEventLog(ev.str());
    }

    ShowUserDialog(dumpPath, reportPath, ok);

    LeaveHandler();
}

LONG CALLBACK VectoredFirstChanceHandler(PEXCEPTION_POINTERS info) {
#if CRASH_ENABLE_VECTORED_FIRST
    // Only record severe first-chance exceptions to avoid noise.
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INVALID_DISPOSITION:
            HandleCrash(info, L"VectoredFirstChance", /*firstChance*/true);
            break;
        default: break;
    }
#else
    (void)info;
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl PurecallHandler() {
    HandleCrash(nullptr, L"Pure virtual function call", false);
    std::abort();
}
void __cdecl InvalidParameterHandler(const wchar_t* expr, const wchar_t* func,
                                     const wchar_t* file, unsigned line, uintptr_t /*res*/) {
    std::wstringstream ss;
    ss << L"Invalid parameter: " << (expr?expr:L"(null)") << L" in "
       << (func?func:L"(func)") << L" at " << (file?file:L"(file)") << L":" << line;
    HandleCrash(nullptr, ss.str().c_str(), false);
    std::abort();
}
[[noreturn]] void __cdecl TerminateHandler() {
    HandleCrash(nullptr, L"std::terminate", false);
    std::abort();
}
void __cdecl NewHandler() {
    HandleCrash(nullptr, L"std::bad_alloc / new handler", false);
    std::abort();
}
void __cdecl SignalHandler(int sig) {
    const wchar_t* name = L"Signal";
    switch (sig) {
        case SIGABRT: name = L"SIGABRT"; break;
        case SIGFPE:  name = L"SIGFPE";  break;
        case SIGILL:  name = L"SIGILL";  break;
        case SIGINT:  name = L"SIGINT";  break;
        case SIGSEGV: name = L"SIGSEGV"; break;
        case SIGTERM: name = L"SIGTERM"; break;
        default: break;
    }
    HandleCrash(nullptr, name, false);
    std::abort();
}

} // anon

// -----------------------------------------------------------------------------
// Public API (matches CrashHandler.h)
// -----------------------------------------------------------------------------
namespace win {

void InstallCrashHandler(const wchar_t* dumpsDir /*= L"crashdumps"*/) {
    std::lock_guard<std::mutex> lock(g.mtx);

    // Resolve dumps directory (relative to exe dir if not absolute)
    std::filesystem::path dd = dumpsDir ? dumpsDir : L"crashdumps";
    if (!dd.is_absolute()) dd = ExeDir() / dd;
    g.dumps_dir = dd;

    GatherExeMetadata();

    // Silence WER popup
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    // Install main unhandled filter
    g.prev_uef = ::SetUnhandledExceptionFilter(&UnhandledCrashFilter);

    // CRT hooks
    _set_purecall_handler(&PurecallHandler);
    _set_invalid_parameter_handler(&InvalidParameterHandler);
    std::set_terminate(&TerminateHandler);
    std::set_new_handler(&NewHandler);

    // Signals
    std::signal(SIGABRT, &SignalHandler);
    std::signal(SIGFPE,  &SignalHandler);
    std::signal(SIGILL,  &SignalHandler);
    std::signal(SIGINT,  &SignalHandler);
    std::signal(SIGSEGV, &SignalHandler);
    std::signal(SIGTERM, &SignalHandler);

#if CRASH_ENABLE_VECTORED_FIRST
    // First-chance handler (advanced/noisy—off by default)
    g.vectored_cookie = ::AddVectoredExceptionHandler(1, &VectoredFirstChanceHandler);
#endif
}

LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* info) {
    // If debugging, pass through so the debugger stops at the fault.
    if (IsDebuggerAttached()) {
        if (g.prev_uef) return g.prev_uef(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    HandleCrash(info, L"UnhandledExceptionFilter", /*firstChance*/false);
    // Consume and terminate
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace win

#endif // _WIN32
