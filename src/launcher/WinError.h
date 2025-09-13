#pragma once
//
// WinError.h — rich Windows error utilities (header-only)
// Windows-only. Requires C++17+.
//

#ifndef _WIN32
#error "WinError.h is Windows-only."
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winternl.h> // for NTSTATUS typedef (no linking needed)
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <cstdint>
#include <cwchar>
#include <cstdio>

namespace colony::winerr {

// ========== Small helpers ==========

struct LastErrorPreserver {
    DWORD saved;
    LastErrorPreserver() noexcept : saved(::GetLastError()) {}
    ~LastErrorPreserver() noexcept { ::SetLastError(saved); }
};

inline std::wstring TrimTrailingWs(std::wstring s) {
    while (!s.empty()) {
        wchar_t c = s.back();
        if (c == L'\r' || c == L'\n' || c == L' ' || c == L'\t') s.pop_back();
        else break;
    }
    return s;
}

inline std::wstring WidenFromUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring out(need, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), need);
    return out;
}

inline std::string NarrowToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int need = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(need, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), need, nullptr, nullptr);
    return out;
}

inline std::wstring NowTimestamp() {
    using namespace std::chrono;
    auto t = system_clock::now();
    std::time_t tt = system_clock::to_time_t(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::wstringstream ss;
    ss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ========== Known names and suggestions ==========

inline const wchar_t* Win32Name(DWORD e) {
    switch (e) {
        case ERROR_SUCCESS: return L"ERROR_SUCCESS";
        case ERROR_INVALID_FUNCTION: return L"ERROR_INVALID_FUNCTION";
        case ERROR_FILE_NOT_FOUND: return L"ERROR_FILE_NOT_FOUND";
        case ERROR_PATH_NOT_FOUND: return L"ERROR_PATH_NOT_FOUND";
        case ERROR_ACCESS_DENIED: return L"ERROR_ACCESS_DENIED";
        case ERROR_INVALID_HANDLE: return L"ERROR_INVALID_HANDLE";
        case ERROR_NOT_ENOUGH_MEMORY: return L"ERROR_NOT_ENOUGH_MEMORY";
        case ERROR_OUTOFMEMORY: return L"ERROR_OUTOFMEMORY";
        case ERROR_INVALID_PARAMETER: return L"ERROR_INVALID_PARAMETER";
        case ERROR_NO_MORE_FILES: return L"ERROR_NO_MORE_FILES";
        case ERROR_WRITE_PROTECT: return L"ERROR_WRITE_PROTECT";
        case ERROR_SHARING_VIOLATION: return L"ERROR_SHARING_VIOLATION";
        case ERROR_LOCK_VIOLATION: return L"ERROR_LOCK_VIOLATION";
        case ERROR_BUSY: return L"ERROR_BUSY";
        case ERROR_ALREADY_EXISTS: return L"ERROR_ALREADY_EXISTS";
        case ERROR_FILENAME_EXCED_RANGE: return L"ERROR_FILENAME_EXCED_RANGE";
        case ERROR_BAD_PATHNAME: return L"ERROR_BAD_PATHNAME";
        case ERROR_BAD_EXE_FORMAT: return L"ERROR_BAD_EXE_FORMAT";
        case ERROR_MOD_NOT_FOUND: return L"ERROR_MOD_NOT_FOUND";
        case ERROR_PROC_NOT_FOUND: return L"ERROR_PROC_NOT_FOUND";
        case ERROR_DLL_INIT_FAILED: return L"ERROR_DLL_INIT_FAILED";
        case ERROR_ENVVAR_NOT_FOUND: return L"ERROR_ENVVAR_NOT_FOUND";
        case ERROR_DIR_NOT_EMPTY: return L"ERROR_DIR_NOT_EMPTY";
        case ERROR_DEV_NOT_EXIST: return L"ERROR_DEV_NOT_EXIST";
        case ERROR_BROKEN_PIPE: return L"ERROR_BROKEN_PIPE";
        case ERROR_MR_MID_NOT_FOUND: return L"ERROR_MR_MID_NOT_FOUND";
        case ERROR_ELEVATION_REQUIRED: return L"ERROR_ELEVATION_REQUIRED";
        default: return nullptr;
    }
}

inline std::wstring Win32Suggestion(DWORD e) {
    switch (e) {
        case ERROR_FILE_NOT_FOUND:
            return L"Verify the path/filename. If relative, ensure the working directory is correct.";
        case ERROR_PATH_NOT_FOUND:
            return L"Create missing directories or correct the relative path/working directory.";
        case ERROR_ACCESS_DENIED:
            return L"Check file/folder permissions or whether another process holds a lock. Avoid admin-only paths.";
        case ERROR_SHARING_VIOLATION:
            return L"Another process is using the file. Close handles or retry with sharing flags.";
        case ERROR_ALREADY_EXISTS:
            return L"Remove/rename existing file or open with appropriate disposition (OPEN_ALWAYS/CREATE_ALWAYS).";
        case ERROR_MOD_NOT_FOUND:
            return L"Required DLL not found. Check installation, PATH, redists, and architecture (x86/x64).";
        case ERROR_PROC_NOT_FOUND:
            return L"Function not exported by the DLL. Check the DLL version matches your SDK/runtime.";
        case ERROR_BAD_EXE_FORMAT:
            return L"Architecture mismatch (e.g., 32‑bit vs 64‑bit). Use the correct binary pair.";
        case ERROR_DLL_INIT_FAILED:
            return L"Module failed to initialize (drivers or environment). Reinstall deps; verify GPU/driver state.";
        case ERROR_INVALID_PARAMETER:
            return L"One or more arguments are invalid. Double‑check flags, sizes, and structure members.";
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return L"System is low on memory or address space. Reduce allocations or use 64‑bit build.";
        case ERROR_ELEVATION_REQUIRED:
            return L"Operation requires elevation. Relaunch as Administrator or adjust UAC policy.";
        default:
            return {};
    }
}

// ========== Facility names for HRESULTs (partial, common set) ==========

inline const wchar_t* HResultFacilityName(uint16_t fac) {
    switch (fac) {
        case 0x0:  return L"FACILITY_NULL";
        case 0x1:  return L"FACILITY_RPC";
        case 0x2:  return L"FACILITY_DISPATCH";
        case 0x3:  return L"FACILITY_STORAGE";
        case 0x4:  return L"FACILITY_ITF";
        case 0x7:  return L"FACILITY_WIN32";
        case 0x8:  return L"FACILITY_WINDOWS";
        case 0x9:  return L"FACILITY_SSPI";
        case 0xA:  return L"FACILITY_CONTROL";
        case 0xB:  return L"FACILITY_CERT";
        case 0xC:  return L"FACILITY_INTERNET";
        case 0xD:  return L"FACILITY_SECURITY";
        case 0xE:  return L"FACILITY_SHELL";
        case 0x10: return L"FACILITY_SETUPAPI";
        case 0x11: return L"FACILITY_MSMQ";
        case 0x12: return L"FACILITY_MEDIA_SERVER";
        case 0x13: return L"FACILITY_MUI";
        case 0x1A: return L"FACILITY_USERMODE_COMMONLOG";
        case 0x1B: return L"FACILITY_WER";
        case 0x1F: return L"FACILITY_DXGI";
        default:   return L"FACILITY_UNKNOWN";
    }
}

inline constexpr uint16_t HrFacility(HRESULT hr) { return uint16_t((uint32_t(hr) >> 16) & 0x1FFFu); }
inline constexpr uint16_t HrCode(HRESULT hr)     { return uint16_t(uint32_t(hr) & 0xFFFFu); }
inline constexpr bool     HrFailed(HRESULT hr)   { return hr < 0; }

// ========== Message formatting (system + common modules) ==========

namespace detail {

inline std::wstring FormatMessageImpl(DWORD code, DWORD flags, HMODULE mod, DWORD langId) {
    LastErrorPreserver guard;
    LPWSTR buf = nullptr;
    DWORD fmFlags = flags | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = ::FormatMessageW(fmFlags, mod, code, langId, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring out;
    if (len && buf) {
        out.assign(buf, len);
        ::LocalFree(buf);
    }
    return TrimTrailingWs(std::move(out));
}

inline std::wstring FormatFromSystem(DWORD code, DWORD langId = 0) {
    return FormatMessageImpl(code, FORMAT_MESSAGE_FROM_SYSTEM, nullptr, langId);
}

inline HMODULE LoadMsgModule(const wchar_t* name) {
    // Prefer an already-loaded handle; else load as datafile so we can read message tables safely.
    if (HMODULE h = ::GetModuleHandleW(name)) return h;
    return ::LoadLibraryExW(name, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
}

inline const std::vector<const wchar_t*>& ModuleNameList() {
    static const std::vector<const wchar_t*> k = {
        L"ntdll.dll",
        L"kernel32.dll",
        L"combase.dll",
        L"ole32.dll",
        L"winhttp.dll",
        L"ws2_32.dll",
        L"netmsg.dll",
        L"netapi32.dll",
        L"iphlpapi.dll",
        L"dxgi.dll",
        L"d3d11.dll",
        L"d3d12.dll",
        L"mfplat.dll",
        L"dwmapi.dll",
        L"crypt32.dll",
        L"wininet.dll",
        L"urlmon.dll"
    };
    return k;
}

inline std::wstring FormatFromKnownModules(DWORD code) {
    static std::once_flag once;
    static std::vector<HMODULE> modules;

    std::call_once(once, []{
        modules.reserve(ModuleNameList().size());
        for (auto name : ModuleNameList()) {
            if (HMODULE h = LoadMsgModule(name)) {
                modules.push_back(h);
            }
        }
        // We intentionally never FreeLibrary() these handles to keep thread-safety simple.
    });

    for (HMODULE h : modules) {
        auto msg = FormatMessageImpl(code, FORMAT_MESSAGE_FROM_HMODULE, h, 0);
        if (!msg.empty()) return msg;
    }
    return {};
}

inline DWORD NtStatusToWin32(NTSTATUS st) {
    using Fn = ULONG (NTAPI*)(NTSTATUS);
    static Fn p = reinterpret_cast<Fn>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
    return p ? p(st) : ERROR_MR_MID_NOT_FOUND;
}

} // namespace detail

// ========== Public formatting APIs ==========

inline std::wstring WinErrToString(DWORD err = ::GetLastError()) {
    if (err == ERROR_SUCCESS) return L"Success";
    if (auto s = detail::FormatFromSystem(err); !s.empty()) return s;
    if (auto s = detail::FormatFromKnownModules(err); !s.empty()) return s;
    std::wstringstream ss;
    ss << L"Unknown error (code " << err << L")";
    return ss.str();
}

inline std::wstring HResultToString(HRESULT hr) {
    // If the HRESULT wraps a Win32 code, try that first.
    if (HrFacility(hr) == 7 /* FACILITY_WIN32 */) {
        DWORD w32 = DWORD(HrCode(hr));
        auto s = WinErrToString(w32);
        if (!s.empty() && s != L"Unknown error") return s;
    }
    // Try system/module message tables for HRESULT itself.
    {
        auto s = detail::FormatFromSystem((DWORD)hr);
        if (!s.empty()) return s;
    }
    {
        auto s = detail::FormatFromKnownModules((DWORD)hr);
        if (!s.empty()) return s;
    }
    std::wstringstream ss;
    ss << L"0x" << std::hex << std::uppercase << (uint32_t)hr << std::dec
       << L" (facility " << HrFacility(hr) << L", code " << HrCode(hr) << L")";
    return ss.str();
}

inline std::wstring NtStatusToString(NTSTATUS st) {
    // Try message tables first.
    if (auto s = detail::FormatFromSystem((DWORD)st); !s.empty()) return s;
    if (auto s = detail::FormatFromKnownModules((DWORD)st); !s.empty()) return s;

    // Try conversion to Win32 via ntdll and look that up.
    DWORD w32 = detail::NtStatusToWin32(st);
    if (w32 != ERROR_MR_MID_NOT_FOUND) {
        auto s = WinErrToString(w32);
        if (!s.empty()) {
            std::wstringstream ss;
            ss << s << L" (derived from NTSTATUS 0x" << std::hex << std::uppercase << (uint32_t)st << L")";
            return ss.str();
        }
    }
    std::wstringstream ss;
    ss << L"NTSTATUS 0x" << std::hex << std::uppercase << (uint32_t)st;
    return ss.str();
}

// ========== Rich error object ==========

struct SourceLocation {
    std::wstring file;
    std::wstring function;
    int          line = 0;
};

inline SourceLocation MakeSourceLocation(const char* file, const char* func, int line) {
    return SourceLocation{ WidenFromUtf8(file), WidenFromUtf8(func), line };
}

enum class Domain : uint8_t { Win32, HResult, NtStatus, Custom };

struct Error {
    Domain      domain = Domain::Custom;
    uint32_t    code   = 0;     // raw code (DWORD / HRESULT / NTSTATUS)
    std::wstring message;       // human-readable text (decoded if possible)
    std::wstring name;          // e.g., ERROR_ACCESS_DENIED or FACILITY name, etc.
    std::wstring suggestion;    // helpful hint when we have one
    SourceLocation where;
    DWORD       pid = ::GetCurrentProcessId();
    DWORD       tid = ::GetCurrentThreadId();
    std::wstring when = NowTimestamp();
    std::wstring context;       // optional user-provided context ("Opening save file")

    static Error FromLastError(const SourceLocation& loc, std::wstring_view context = {}) {
        DWORD e = ::GetLastError();
        Error out;
        out.domain = Domain::Win32;
        out.code = e;
        out.message = WinErrToString(e);
        if (auto n = Win32Name(e)) out.name = n;
        out.suggestion = Win32Suggestion(e);
        out.where = loc;
        out.context.assign(context.begin(), context.end());
        return out;
    }

    static Error FromWin32(DWORD e, const SourceLocation& loc, std::wstring_view context = {}) {
        Error out;
        out.domain = Domain::Win32;
        out.code = e;
        out.message = WinErrToString(e);
        if (auto n = Win32Name(e)) out.name = n;
        out.suggestion = Win32Suggestion(e);
        out.where = loc;
        out.context.assign(context.begin(), context.end());
        return out;
    }

    static Error FromHRESULT(HRESULT hr, const SourceLocation& loc, std::wstring_view context = {}) {
        Error out;
        out.domain = Domain::HResult;
        out.code = static_cast<uint32_t>(hr);
        out.message = HResultToString(hr);

        // Provide a name/facility summary:
        std::wstringstream n;
        n << L"HRESULT 0x" << std::hex << std::uppercase << (uint32_t)hr
          << L" (" << HResultFacilityName(HrFacility(hr)) << L")";
        out.name = n.str();

        // Simple suggestions for a few common HRESULT patterns:
        if (HrFacility(hr) == 7 /*WIN32*/) out.suggestion = Win32Suggestion(DWORD(HrCode(hr)));

        out.where = loc;
        out.context.assign(context.begin(), context.end());
        return out;
    }

    static Error FromNtStatus(NTSTATUS st, const SourceLocation& loc, std::wstring_view context = {}) {
        Error out;
        out.domain = Domain::NtStatus;
        out.code = static_cast<uint32_t>(st);
        out.message = NtStatusToString(st);
        {
            std::wstringstream n;
            n << L"NTSTATUS 0x" << std::hex << std::uppercase << (uint32_t)st;
            out.name = n.str();
        }
        out.where = loc;
        out.context.assign(context.begin(), context.end());
        return out;
    }

    // Compose a detailed multi-line description (Unicode).
    std::wstring ToWString(bool includeContext = true) const {
        std::wstringstream ss;
        ss << L"[Error] " << DomainName() << L" ";
        switch (domain) {
            case Domain::Win32:  ss << L"(code " << code << L")"; break;
            case Domain::HResult: ss << L"(hr 0x" << std::hex << std::uppercase << code << std::dec << L")"; break;
            case Domain::NtStatus: ss << L"(ntstatus 0x" << std::hex << std::uppercase << code << std::dec << L")"; break;
            default: ss << L"(code " << code << L")"; break;
        }
        if (!name.empty()) ss << L" " << name;
        ss << L": " << (message.empty() ? L"(no message)" : message);

        if (!suggestion.empty()) ss << L"\nHint: " << suggestion;

        ss << L"\nWhere: " << where.file << L"(" << where.line << L") in " << where.function;
        ss << L"\nWhen:  " << when << L"  PID " << pid << L"  TID " << tid;

        if (includeContext && !context.empty()) ss << L"\nCtx:   " << context;

        return ss.str();
    }

    std::string ToUtf8(bool includeContext = true) const {
        return NarrowToUtf8(ToWString(includeContext));
    }

    const wchar_t* DomainName() const {
        switch (domain) {
            case Domain::Win32:  return L"Win32";
            case Domain::HResult: return L"HRESULT";
            case Domain::NtStatus: return L"NTSTATUS";
            default: return L"Custom";
        }
    }
};

// ========== Convenience macros ==========
//
// Use these to capture file/func/line automatically when creating an Error.
//

#define WINERR_HERE() ::colony::winerr::MakeSourceLocation(__FILE__, __FUNCTION__, __LINE__)
#define WINERR_FROM_LAST(ctx_wide_literal) ::colony::winerr::Error::FromLastError(WINERR_HERE(), (ctx_wide_literal))
#define WINERR_FROM_WIN32(code, ctx_wide_literal) ::colony::winerr::Error::FromWin32((code), WINERR_HERE(), (ctx_wide_literal))
#define WINERR_FROM_HR(hr, ctx_wide_literal) ::colony::winerr::Error::FromHRESULT((hr), WINERR_HERE(), (ctx_wide_literal))
#define WINERR_FROM_NT(st, ctx_wide_literal) ::colony::winerr::Error::FromNtStatus((st), WINERR_HERE(), (ctx_wide_literal))

// ========== Lightweight result helper ==========
//
// Pattern:
//   auto [ok, err] = TrySomething(...);
//   if (!ok) { MessageBoxW(nullptr, err.ToWString().c_str(), L"Oops", MB_OK | MB_ICONERROR); }
//
// You can return this from functions that may fail without throwing.
//

struct Result {
    bool ok = true;
    Error err{};
    static Result Success() { return Result{ true, {} }; }
    static Result Fail(const Error& e) { return Result{ false, e }; }
};

// ========== One-liners for quick messages ==========
//
// Keep your original signature for backward compatibility.
// These do not include context/source info.
//

inline std::wstring SimpleWinErrToString(DWORD err = ::GetLastError()) {
    return WinErrToString(err);
}

inline std::wstring SimpleHResultToString(HRESULT hr) {
    return HResultToString(hr);
}

inline std::wstring SimpleNtStatusToString(NTSTATUS st) {
    return NtStatusToString(st);
}

} // namespace colony::winerr

/* ============================ Usage examples ============================

#include "WinError.h"
using namespace colony::winerr;

// 1) Quick: preserve old call-site behavior
std::wstring s = SimpleWinErrToString(); // uses GetLastError()

// 2) Rich error with context + source location
HANDLE h = ::CreateFileW(L"nonexistent.file", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
if (h == INVALID_HANDLE_VALUE) {
    Error e = WINERR_FROM_LAST(L"Opening level file");
    ::MessageBoxW(nullptr, e.ToWString().c_str(), L"Colony Launcher", MB_OK | MB_ICONERROR);
}

// 3) HRESULT (e.g., from COM / DirectX)
HRESULT hr = E_ACCESSDENIED;
Error eh = WINERR_FROM_HR(hr, L"Initializing COM");
OutputDebugStringW((eh.ToWString() + L"\n").c_str());

// 4) NTSTATUS (if you call lower-level APIs)
NTSTATUS st = 0xC0000005; // STATUS_ACCESS_VIOLATION
Error en = WINERR_FROM_NT(st, L"Low-level system call");
auto utf8 = en.ToUtf8();

======================================================================== */
