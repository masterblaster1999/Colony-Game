// src/platform/win/pch_win.h
#pragma once

// ----- Platform gate ---------------------------------------------------------
#if !defined(_WIN32)
#  error "pch_win.h is Windows-only."
#endif

// ----- Feature toggles (define before including this header to change) -------
#ifndef CG_WIN_ENABLE_DPI
#  define CG_WIN_ENABLE_DPI 1
#endif
#ifndef CG_WIN_ENABLE_DXGI
#  define CG_WIN_ENABLE_DXGI 1
#endif
#ifndef CG_WIN_ENABLE_COM
#  define CG_WIN_ENABLE_COM 1
#endif
#ifndef CG_WIN_ENABLE_CRTDBG
#  if defined(_DEBUG)
#    define CG_WIN_ENABLE_CRTDBG 1
#  else
#    define CG_WIN_ENABLE_CRTDBG 0
#  endif
#endif

// ----- Windows macro hygiene -------------------------------------------------
// Stricter type checking for GDI/USER handles, per Microsoft docs.
#ifndef STRICT
#  define STRICT 1
#endif

// Remove rarely-used APIs from <windows.h>.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif

// Prevent min/max macro collisions with <algorithm>.
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif

// Define the minimum OS by default (Windows 10). You can override in build sys.
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#ifndef WINVER
#  define WINVER _WIN32_WINNT
#endif

// If you prefer ANSI <-> UNICODE switching at build level, define UNICODE in CMake.
// We avoid forcing UNICODE here to not surprise existing code.

// ----- System headers (order matters) ----------------------------------------
#include <sdkddkver.h>
#include <sal.h>
#include <windows.h>

#include <wrl/client.h>      // Microsoft::WRL::ComPtr
#include <versionhelpers.h>  // IsWindows10OrGreater, etc.

#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>
#include <optional>
#include <type_traits>
#include <utility>
#include <chrono>

// Common Win32 sub-headers used by the helpers below
#include <debugapi.h>        // OutputDebugString
#include <errhandlingapi.h>  // GetLastError
#include <winuser.h>         // DPI_AWARENESS_CONTEXT (if available)
#include <profileapi.h>      // QPC
#include <stringapiset.h>    // MultiByteToWideChar / WideCharToMultiByte
#include <handleapi.h>       // CloseHandle
#include <processthreadsapi.h> // SetThreadDescription (dyn-linked on older OS)

// Optional sections
#if CG_WIN_ENABLE_DPI
  #include <shellscalingapi.h> // GetDpiForMonitor / SetProcessDpiAwareness
#endif

#if CG_WIN_ENABLE_DXGI
  #include <dxgi1_5.h> // DXGI_FEATURE_PRESENT_ALLOW_TEARING (Factory5)
#endif

#if CG_WIN_ENABLE_CRTDBG
  #include <crtdbg.h>   // CRT leak detection in Debug
#endif

// ----- Attributes / macros ---------------------------------------------------
#if defined(_MSC_VER)
#  define CG_ALWAYS_INLINE __forceinline
#else
#  define CG_ALWAYS_INLINE inline
#endif

#ifndef CG_NODISCARD
#  define CG_NODISCARD [[nodiscard]]
#endif

#define CG_WIN_WIDEN2(x) L##x
#define CG_WIN_WIDEN(x)  CG_WIN_WIDEN2(x)
#define CG_WIN_WFILE     CG_WIN_WIDEN(__FILE__)

// ----- Namespace -------------------------------------------------------------
namespace cg::win {

// ============================================================================
// Debug output & error formatting
// ============================================================================

namespace detail {
    CG_ALWAYS_INLINE inline void dbg_print_impl(const wchar_t* s) noexcept {
        ::OutputDebugStringW(s);
        ::OutputDebugStringW(L"\n");
    }
}

// printf-style minimal formatter for wide strings (avoid iostreams in PCH)
template <typename... Args>
inline void dprintf(const wchar_t* fmt, Args... args) {
    // Reasonable fixed buffer; fall back to heap if needed.
    wchar_t stackBuf[512];
    int count = _snwprintf_s(stackBuf, _countof(stackBuf), _TRUNCATE, fmt, args...);
    if (count >= 0) {
        detail::dbg_print_impl(stackBuf);
    } else {
        // allocate
        int needed = _snwprintf(nullptr, 0, fmt, args...);
        if (needed > 0) {
            std::wstring buf(static_cast<size_t>(needed) + 1, L'\0');
            _snwprintf(buf.data(), buf.size(), fmt, args...);
            detail::dbg_print_impl(buf.c_str());
        }
    }
}

// Convert GetLastError() to readable std::wstring
inline std::wstring last_error_message(DWORD le = ::GetLastError()) {
    LPWSTR m = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = ::FormatMessageW(flags, nullptr, le, 0, reinterpret_cast<LPWSTR>(&m), 0, nullptr);
    std::wstring out = (len && m) ? std::wstring(m, len) : L"Unknown error";
    if (m) ::LocalFree(m);
    return out;
}

// Convert HRESULT to message (includes hex code)
inline std::wstring hr_message(HRESULT hr) {
    LPWSTR m = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = ::FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&m), 0, nullptr);
    std::wstring out = L"HRESULT 0x" + std::to_wstring(static_cast<unsigned long>(hr));
    if (len && m) {
        out += L": ";
        out.append(m, len);
    }
    if (m) ::LocalFree(m);
    return out;
}

// Hardened Win32/HRESULT check macros: logs and breaks in Debug; still returns value in Release.
namespace detail {
    CG_ALWAYS_INLINE inline HRESULT HrCheck(HRESULT hr, const wchar_t* expr, const wchar_t* file, int line) {
        if (FAILED(hr)) {
#if defined(_DEBUG)
            dprintf(L"[CG_HR] FAILED: %s at %s(%d): %s", expr, file, line, hr_message(hr).c_str());
            __debugbreak();
#endif
        }
        return hr;
    }

    CG_ALWAYS_INLINE inline BOOL Win32Check(BOOL ok, const wchar_t* expr, const wchar_t* file, int line) {
        if (!ok) {
#if defined(_DEBUG)
            const DWORD le = ::GetLastError();
            dprintf(L"[CG_WIN32] FAILED: %s at %s(%d): (GetLastError=%lu) %s", expr, file, line, le, last_error_message(le).c_str());
            __debugbreak();
#endif
        }
        return ok;
    }
} // namespace detail

#define CG_HR(expr)      (::cg::win::detail::HrCheck( (expr), L#expr, CG_WIN_WFILE, __LINE__ ))
#define CG_WIN32(expr)   (::cg::win::detail::Win32Check( (expr), L#expr, CG_WIN_WFILE, __LINE__ ))

// ============================================================================
// HANDLE RAII
// ============================================================================

template<auto CloseFn, HANDLE Invalid = INVALID_HANDLE_VALUE>
class UniqueHandleT {
public:
    using handle_type = HANDLE;

    constexpr UniqueHandleT() noexcept = default;
    explicit constexpr UniqueHandleT(handle_type h) noexcept : h_(h) {}
    ~UniqueHandleT() { reset(); }

    UniqueHandleT(const UniqueHandleT&) = delete;
    UniqueHandleT& operator=(const UniqueHandleT&) = delete;

    UniqueHandleT(UniqueHandleT&& o) noexcept : h_(o.release()) {}
    UniqueHandleT& operator=(UniqueHandleT&& o) noexcept {
        if (this != &o) { reset(o.release()); }
        return *this;
    }

    CG_NODISCARD constexpr handle_type get()  const noexcept { return h_; }
    explicit constexpr operator bool() const noexcept { return h_ && h_ != Invalid; }

    handle_type release() noexcept { handle_type t = h_; h_ = Invalid; return t; }
    void reset(handle_type nh = Invalid) noexcept {
        if (h_ && h_ != Invalid) { CloseFn(h_); }
        h_ = nh;
    }

private:
    handle_type h_ = Invalid;
};

using UniqueHandle     = UniqueHandleT<&::CloseHandle>;
using UniqueFindHandle = UniqueHandleT<&::FindClose>;

// ============================================================================
// UTF-8 <-> UTF-16 conversions (strict)
// ============================================================================

CG_NODISCARD inline bool utf8_to_wide(std::string_view in, std::wstring& out) {
    if (in.empty()) { out.clear(); return true; }
    int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), nullptr, 0);
    if (needed <= 0) return false;
    out.resize(static_cast<size_t>(needed));
    int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), out.data(), needed);
    return written == needed;
}

CG_NODISCARD inline bool wide_to_utf8(std::wstring_view in, std::string& out) {
    if (in.empty()) { out.clear(); return true; }
    int needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;
    out.resize(static_cast<size_t>(needed));
    int written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), out.data(), needed, nullptr, nullptr);
    return written == needed;
}

// Convenience overloads
CG_NODISCARD inline std::wstring utf8_to_wide(std::string_view in) {
    std::wstring out; utf8_to_wide(in, out); return out;
}
CG_NODISCARD inline std::string wide_to_utf8(std::wstring_view in) {
    std::string out; wide_to_utf8(in, out); return out;
}

// ============================================================================
// High-resolution timing (QPC)
// ============================================================================

struct QpcClock {
    static inline int64_t frequency() noexcept {
        static int64_t f = []{
            LARGE_INTEGER li{};
            ::QueryPerformanceFrequency(&li);
            return li.QuadPart;
        }();
        return f;
    }

    static inline int64_t now_ticks() noexcept {
        LARGE_INTEGER li{};
        ::QueryPerformanceCounter(&li);
        return li.QuadPart;
    }

    static inline double to_seconds(int64_t ticks) noexcept {
        return static_cast<double>(ticks) / static_cast<double>(frequency());
    }
};

struct ScopedQpc {
    int64_t start = QpcClock::now_ticks();
    double  elapsed_s() const noexcept { return QpcClock::to_seconds(QpcClock::now_ticks() - start); }
};

// ============================================================================
// COM: apartment RAII (opt-in)
// ============================================================================

#if CG_WIN_ENABLE_COM
class ComInitializer {
public:
    explicit ComInitializer(DWORD coinit = COINIT_MULTITHREADED) noexcept
        : hr_(::CoInitializeEx(nullptr, coinit)) {}
    ~ComInitializer() { if (SUCCEEDED(hr_)) ::CoUninitialize(); }

    CG_NODISCARD bool ok() const noexcept { return SUCCEEDED(hr_); }
    CG_NODISCARD HRESULT hr() const noexcept { return hr_; }

private:
    HRESULT hr_{ E_FAIL };
};
#endif // CG_WIN_ENABLE_COM

// ============================================================================
// Thread naming (SetThreadDescription, fallback to 0x406D1388)
// ============================================================================

inline void set_current_thread_name(std::wstring_view name) {
    // Prefer SetThreadDescription when available.
    using PFN_SetThreadDescription = HRESULT (WINAPI*)(HANDLE, PCWSTR);
    static auto pSetThreadDescription =
        reinterpret_cast<PFN_SetThreadDescription>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    if (pSetThreadDescription) {
        (void)pSetThreadDescription(::GetCurrentThread(), std::wstring(name).c_str());
        return;
    }

    // Fallback: raise MSVC thread-naming exception so debuggers pick it up.
    // Ref: Visual Studio docs on thread naming exception 0x406D1388.
#pragma pack(push, 8)
    struct THREADNAME_INFO {
        DWORD dwType;     // 0x1000
        LPCSTR szName;    // ANSI is expected here
        DWORD dwThreadID; // -1 for current thread
        DWORD dwFlags;    // 0
    };
#pragma pack(pop)
    std::string narrow = wide_to_utf8(std::wstring(name));
    THREADNAME_INFO info = { 0x1000, narrow.c_str(), static_cast<DWORD>(-1), 0 };
    __try {
        ::RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // ignore
    }
}

// ============================================================================
// High DPI helpers (Per-Monitor v2 if possible)
// ============================================================================

#if CG_WIN_ENABLE_DPI
inline bool set_process_dpi_awareness_per_monitor_v2() {
    // Recommended to use manifest when feasible, but provide a runtime helper too.
    using PFN_SetProcessDpiAwarenessContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto p = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (p) {
        if (p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return true;
    }

    // Windows 8.1 fallback
    using PFN_SetProcessDpiAwareness = HRESULT (WINAPI*)(PROCESS_DPI_AWARENESS);
    HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        auto p2 = reinterpret_cast<PFN_SetProcessDpiAwareness>(::GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (p2) {
            if (SUCCEEDED(p2(PROCESS_PER_MONITOR_DPI_AWARE))) { ::FreeLibrary(shcore); return true; }
        }
        ::FreeLibrary(shcore);
    }

    // Legacy fallback (system DPI aware)
    return ::SetProcessDPIAware() ? true : false;
}

// Get effective DPI for a HWND (96.0f if unavailable)
inline float get_dpi_for_window(HWND h) {
    // Win10+: GetDpiForWindow
    using PFN_GetDpiForWindow = UINT (WINAPI*)(HWND);
    auto p = reinterpret_cast<PFN_GetDpiForWindow>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (p) return static_cast<float>(p(h));

    // Win8.1: GetDpiForMonitor
    HMONITOR mon = ::MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    UINT x=96, y=96;
    if (mon) {
        auto p2 = reinterpret_cast<HRESULT (WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*)>(
            ::GetProcAddress(::GetModuleHandleW(L"Shcore.dll"), "GetDpiForMonitor"));
        if (p2 && SUCCEEDED(p2(mon, MDT_EFFECTIVE_DPI, &x, &y))) {
            return static_cast<float>(x);
        }
    }
    return 96.0f;
}
#endif // CG_WIN_ENABLE_DPI

// ============================================================================
// DXGI helpers (tearing / VRR support query)
// ============================================================================

#if CG_WIN_ENABLE_DXGI
inline bool dxgi_allow_tearing_supported() {
    BOOL allow = FALSE;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory1;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory1)))) return false;

    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (FAILED(factory1.As(&factory5)) || !factory5) return false;

    if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
        return allow == TRUE;
    }
    return false;
}
#endif // CG_WIN_ENABLE_DXGI

// ============================================================================
// CRT Debug Heap (Debug-only opt-in)
// ============================================================================

#if CG_WIN_ENABLE_CRTDBG
inline void enable_crt_leak_checks() {
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);
}
#endif // CG_WIN_ENABLE_CRTDBG

} // namespace cg::win
