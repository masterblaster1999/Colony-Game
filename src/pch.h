#pragma once

// =================================================================================================
// [Colony Game PCH] Windows-only (MSVC) precompiled header
// - Centralizes platform tuning macros before <Windows.h>
// - Unifies WRL ComPtr usage via a single alias under cg:: (no global alias)
// - Gathers widely used STL/Win32/DX11/SDL2/EnTT/fmt/spdlog headers
// - Adds small, header-only Win32/D3D11 utilities under namespace cg::win
//
// Linking for WinMM/DbgHelp/Shcore/etc. is handled in CMake. This file never uses #pragma comment(lib,...).
// =================================================================================================

// ----- Platform macros (define before including any Windows headers) ------------------------------
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef STRICT
#  define STRICT 1
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

// =================================================================================================
// C++ standard library (safe to put in PCH)
// =================================================================================================
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <deque>
#include <queue>
#include <stack>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <span>
#include <filesystem>
#include <chrono>
#include <limits>
#include <atomic>
#include <mutex>
#include <system_error>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <iterator>
#include <new>
#include <cstdio>
#include <cstdarg>

// =================================================================================================
/* Windows & friends (order matters) */
// =================================================================================================
#include <sdkddkver.h>
#include <sal.h>
#include <Windows.h>
#include <libloaderapi.h>        // GetModuleFileNameW, LoadLibraryW
#include <processthreadsapi.h>   // SetThreadDescription, priorities
#include <synchapi.h>            // Sleep, WaitForSingleObject
#include <shellapi.h>            // CommandLineToArgvW
#include <shlobj_core.h>         // SHGetKnownFolderPath
#include <debugapi.h>            // OutputDebugString
#include <combaseapi.h>          // CoInitializeEx, CoTaskMemFree
#include <errhandlingapi.h>      // GetLastError
#include <timeapi.h>             // timeBeginPeriod/timeEndPeriod (winmm)
#include <VersionHelpers.h>      // IsWindows* helpers
#include <winreg.h>              // Registry helpers
#include <wrl/client.h>          // Microsoft::WRL::ComPtr
#include <shellscalingapi.h>     // PROCESS_DPI_AWARENESS, etc. (link Shcore.lib)

// =================================================================================================
// DirectX 11 / DXGI / Math
// =================================================================================================
#ifndef CG_PCH_ENABLE_D3D11
#  define CG_PCH_ENABLE_D3D11 1
#endif
#ifndef CG_PCH_ENABLE_HLSL_COMPILE
#  define CG_PCH_ENABLE_HLSL_COMPILE 1
#endif
#ifndef CG_PCH_ENABLE_DXGI_DEBUG
#  define CG_PCH_ENABLE_DXGI_DEBUG 0
#endif

#if CG_PCH_ENABLE_D3D11
  #include <d3d11.h>
  #if __has_include(<dxgi1_6.h>)
    #include <dxgi1_6.h>
  #elif __has_include(<dxgi1_5.h>)
    #include <dxgi1_5.h>
  #elif __has_include(<dxgi1_4.h>)
    #include <dxgi1_4.h>
  #else
    #include <dxgi.h>
  #endif
  #if __has_include(<d3dcompiler.h>)
    #include <d3dcompiler.h>     // runtime shader compile helper (see cg::win::hlsl_compile)
  #endif
  #include <DirectXMath.h>
  #include <DirectXColors.h>
  #if CG_PCH_ENABLE_DXGI_DEBUG
    #if __has_include(<dxgidebug.h>)
      #include <dxgidebug.h>
    #endif
  #endif
#endif

// =================================================================================================
// WIC (for PNG/JPEG encode/decode GUIDs like GUID_ContainerFormatPng)
// =================================================================================================
#ifndef CG_PCH_ENABLE_WIC
#  define CG_PCH_ENABLE_WIC 1
#endif
#if CG_PCH_ENABLE_WIC
  #if __has_include(<wincodec.h>)
    #include <wincodec.h>        // IWIC*, GUID_ContainerFormatPng
  #endif
#endif

// =================================================================================================
// XAudio2 (optional)
// =================================================================================================
#ifndef CG_PCH_ENABLE_XAUDIO2
#  define CG_PCH_ENABLE_XAUDIO2 1
#endif
#if CG_PCH_ENABLE_XAUDIO2
  #if __has_include(<xaudio2.h>)
    #include <xaudio2.h> // XAudio2.9 (Windows SDK)
    #define CG_HAS_XAUDIO2 1
  #else
    #define CG_HAS_XAUDIO2 0
  #endif
#endif

// =================================================================================================
// Debug help (MiniDump / optional stack tracing)
// =================================================================================================
#ifndef CG_PCH_ENABLE_DBGHELP
#  define CG_PCH_ENABLE_DBGHELP 1
#endif
#ifndef CG_PCH_ENABLE_STACKTRACE
#  define CG_PCH_ENABLE_STACKTRACE 0
#endif

#if CG_PCH_ENABLE_DBGHELP
  #include <dbghelp.h>
#endif

// =================================================================================================
/* SDL2 */
// =================================================================================================
#if __has_include(<SDL.h>)
#  include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#  include <SDL2/SDL.h>
#endif

// =================================================================================================
/* Logging (fmt + spdlog) */
// =================================================================================================
#ifndef SPDLOG_ACTIVE_LEVEL
#  ifdef NDEBUG
#    define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#  else
#    define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#  endif
#endif
#if __has_include(<fmt/core.h>)
#  include <fmt/core.h>
#  include <fmt/format.h>
#endif
#if __has_include(<spdlog/spdlog.h>)
#  include <spdlog/spdlog.h>
#  include <spdlog/fmt/ostr.h>
#endif

// =================================================================================================
/* EnTT ECS */
// =================================================================================================
#if __has_include(<entt/entt.hpp>)
#  include <entt/entt.hpp>
#endif

// =================================================================================================
/* Taskflow (job system) */
// =================================================================================================
#if __has_include(<taskflow/taskflow.hpp>)
#  include <taskflow/taskflow.hpp>
#endif

// =================================================================================================
/* Dear ImGui (and backends, if present in include path) */
// =================================================================================================
#if __has_include(<imgui.h>)
#  include <imgui.h>
#endif
#if __has_include(<backends/imgui_impl_win32.h>)
#  include <backends/imgui_impl_win32.h>
#endif
#if __has_include(<backends/imgui_impl_dx11.h>)
#  include <backends/imgui_impl_dx11.h>
#endif

// =================================================================================================
// Short-hands & attributes used pervasively
// =================================================================================================
#define CG_NODISCARD [[nodiscard]]
#define CG_UNUSED(x) (void)(x)

#ifndef CG_ALWAYS_INLINE
#  if defined(_MSC_VER)
#    define CG_ALWAYS_INLINE __forceinline
#  else
#    define CG_ALWAYS_INLINE inline
#  endif
#endif

#ifndef CG_NO_COPY_MOVE
#  define CG_NO_COPY_MOVE(T)        \
    T(const T&)            = delete; \
    T& operator=(const T&) = delete; \
    T(T&&)                 = delete; \
    T& operator=(T&&)      = delete
#endif

#if defined(__cpp_attributes) && __cpp_attributes >= 200809
#  define CG_LIKELY(x)   (x) [[likely]]
#  define CG_UNLIKELY(x) (x) [[unlikely]]
#else
#  define CG_LIKELY(x)   (x)
#  define CG_UNLIKELY(x) (x)
#endif

#ifndef CG_DEBUG_BREAK
#  ifdef _DEBUG
#    define CG_DEBUG_BREAK() __debugbreak()
#  else
#    define CG_DEBUG_BREAK() ((void)0)
#  endif
#endif

// ---- WRL smart pointer alias lives under cg:: to avoid global collisions (C2874) ----------------
namespace cg {
  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;
}

// -------------------------------------------------------------------------------------------------
// Tiny scope guard (DEFER)
// -------------------------------------------------------------------------------------------------
namespace cg::detail {
  template<class F>
  class scope_exit {
  public:
    explicit scope_exit(F&& f) noexcept(std::is_nothrow_move_constructible_v<F>) : f_(std::move(f)) {}
    ~scope_exit() noexcept { if (armed_) f_(); }
    scope_exit(const scope_exit&)            = delete;
    scope_exit& operator=(const scope_exit&) = delete;
    void release() noexcept { armed_ = false; }
  private:
    F    f_;
    bool armed_ {true};
  };
  template<class F> scope_exit(F) -> scope_exit<F>;
}
#define CG_CONCAT_INNER(a,b) a##b
#define CG_CONCAT(a,b) CG_CONCAT_INNER(a,b)
#define CG_DEFER auto CG_CONCAT(_defer_, __COUNTER__) = ::cg::detail::scope_exit([&]()

// -------------------------------------------------------------------------------------------------
// cg::win — header-only helpers (RAII + utilities for Win32 / D3D11 / etc.)
// -------------------------------------------------------------------------------------------------
namespace cg::win {

using ::cg::ComPtr; // keep short 'ComPtr<...>' inside cg::win helpers

// ---------- RAII: COM initialization ----------
class ComInit {
public:
  explicit ComInit(DWORD coinit = COINIT_APARTMENTTHREADED) noexcept
    : ok_(SUCCEEDED(::CoInitializeEx(nullptr, coinit))) {}
  ~ComInit() { if (ok_) ::CoUninitialize(); }
  CG_NO_COPY_MOVE(ComInit);
  bool ok() const noexcept { return ok_; }
private:
  bool ok_{false};
};

// ---------- RAII: Save/restore LastError ----------
class LastErrorPreserver {
public:
  LastErrorPreserver()  noexcept : err_(::GetLastError()) {}
  ~LastErrorPreserver() noexcept { ::SetLastError(err_); }
  CG_NO_COPY_MOVE(LastErrorPreserver);
private:
  DWORD err_;
};

// ---------- RAII: HANDLE ----------
class Handle {
public:
  Handle() noexcept = default;
  explicit Handle(HANDLE h) noexcept : h_(h) {}
  ~Handle() { reset(); }
  CG_NO_COPY_MOVE(Handle);
  HANDLE get() const noexcept { return h_; }
  explicit operator bool() const noexcept { return h_ && h_ != INVALID_HANDLE_VALUE; }
  void reset(HANDLE h = nullptr) noexcept {
    if (h_ && h_ != INVALID_HANDLE_VALUE) ::CloseHandle(h_);
    h_ = h;
  }
  HANDLE release() noexcept { HANDLE t = h_; h_ = nullptr; return t; }
private:
  HANDLE h_{}; // nullptr | INVALID_HANDLE_VALUE means "empty"
};

// ---------- RAII: HMODULE ----------
class HModule {
public:
  HModule() noexcept = default;
  explicit HModule(HMODULE m) noexcept : m_(m) {}
  static HModule load(const wchar_t* name) noexcept { return HModule(::LoadLibraryW(name)); }
  ~HModule() { if (m_) ::FreeLibrary(m_); }
  CG_NO_COPY_MOVE(HModule);
  HMODULE get() const noexcept { return m_; }
  FARPROC proc(const char* name) const noexcept { return m_ ? ::GetProcAddress(m_, name) : nullptr; }
private:
  HMODULE m_{};
};

// ---------- Unicode conversions (UTF-8 <-> UTF-16) ----------
CG_ALWAYS_INLINE std::wstring utf16_from_utf8(std::string_view s) {
  if (s.empty()) return {};
  int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                     static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  int w = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                static_cast<int>(s.size()), out.data(), needed);
  if (w <= 0) return {};
  return out;
}
CG_ALWAYS_INLINE std::string utf8_from_utf16(std::wstring_view ws) {
  if (ws.empty()) return {};
  int needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws.data(),
                                     static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  int n = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws.data(),
                                static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
  if (n <= 0) return {};
  return out;
}
CG_ALWAYS_INLINE std::wstring widen (std::string_view  s) { return utf16_from_utf8(s); }
CG_ALWAYS_INLINE std::string  narrow(std::wstring_view s) { return utf8_from_utf16(s); }

// ---------- Error helpers ----------
CG_ALWAYS_INLINE std::wstring format_message_w(DWORD code) {
  LPWSTR buf = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                    | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = ::FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
  std::wstring out;
  if (len && buf) {
    out.assign(buf, buf + len);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' '))
      out.pop_back();
  }
  if (buf) ::LocalFree(buf);
  return out;
}
CG_ALWAYS_INLINE std::wstring last_error_w(DWORD code = ::GetLastError()) { return format_message_w(code); }
CG_ALWAYS_INLINE std::string  last_error_u8(DWORD code = ::GetLastError()) { return narrow(last_error_w(code)); }

CG_ALWAYS_INLINE bool hr_ok(HRESULT hr, const wchar_t* context = L"") {
#ifdef _DEBUG
  if (FAILED(hr)) {
    wchar_t code[16]{}; _snwprintf_s(code, _TRUNCATE, L"0x%08X", static_cast<unsigned>(hr));
    std::wstring msg = L"[HR] ";
    if (context && *context) { msg += context; msg += L" "; }
    msg += code; msg += L" ";
    std::wstring sys = format_message_w(static_cast<DWORD>(hr));
    if (!sys.empty()) { msg += L"— "; msg += sys; }
    msg += L"\n";
    ::OutputDebugStringW(msg.c_str());
    return false;
  }
#endif
  return SUCCEEDED(hr);
}
#ifndef CG_HR
#  define CG_HR(expr) ::cg::win::hr_ok((expr), L#expr)
#endif

// ---------- Debug trace + printf ----------
CG_ALWAYS_INLINE void debug_trace(std::wstring_view s) {
#ifdef _DEBUG
  ::OutputDebugStringW(s.data());
  if (!s.empty() && s.back() != L'\n') ::OutputDebugStringW(L"\n");
#endif
}
CG_ALWAYS_INLINE void debug_trace(std::string_view s) {
#ifdef _DEBUG
  auto w = widen(s);
  ::OutputDebugStringW(w.c_str());
  if (!s.empty() && s.back() != '\n') ::OutputDebugStringW(L"\n");
#endif
}
CG_ALWAYS_INLINE void debug_printf(const wchar_t* fmt, ...) {
#ifdef _DEBUG
  wchar_t buf[1024];
  va_list ap; va_start(ap, fmt);
  _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
  va_end(ap);
  ::OutputDebugStringW(buf);
  if (buf[0] && buf[wcslen(buf)-1] != L'\n') ::OutputDebugStringW(L"\n");
#endif
}

// Lightweight log macros (can be unified later with your engine logger)
#ifndef CG_LOG_ENABLED
#  define CG_LOG_ENABLED 1
#endif
#if CG_LOG_ENABLED
#  define CG_LOGI(msg) ::cg::win::debug_trace(std::wstring(L"[I] ") + (msg))
#  define CG_LOGW(msg) ::cg::win::debug_trace(std::wstring(L"[W] ") + (msg))
#  define CG_LOGE(msg) ::cg::win::debug_trace(std::wstring(L"[E] ") + (msg))
#else
#  define CG_LOGI(msg) ((void)0)
#  define CG_LOGW(msg) ((void)0)
#  define CG_LOGE(msg) ((void)0)
#endif

// ---------- Known Folders & paths ----------
CG_ALWAYS_INLINE std::filesystem::path known_folder(const KNOWNFOLDERID& id,
                                                    DWORD flags = KF_FLAG_DEFAULT) {
  PWSTR p = nullptr;
  std::filesystem::path result;
  if (SUCCEEDED(::SHGetKnownFolderPath(id, flags, nullptr, &p)) && p) {
    result = p;
    ::CoTaskMemFree(p);
  }
  return result;
}
CG_ALWAYS_INLINE std::filesystem::path local_appdata_subdir(std::wstring_view sub) {
  auto base = known_folder(FOLDERID_LocalAppData, KF_FLAG_CREATE);
  auto dir  = base / sub;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// Long-path helper (adds \\?\ prefix to absolute paths when not already present)
CG_ALWAYS_INLINE std::filesystem::path ensure_long_path(const std::filesystem::path& p) {
  auto w = p.wstring();
  if (w.rfind(L"\\\\?\\", 0) == 0 || w.rfind(L"\\\\.\\", 0) == 0) return p;
  if (p.is_absolute()) return std::filesystem::path(L"\\\\?\\") / p;
  return p;
}

// ---------- Executable & module paths ----------
CG_ALWAYS_INLINE std::filesystem::path module_path(HMODULE hMod = nullptr) {
  wchar_t buf[MAX_PATH];
  DWORD n = ::GetModuleFileNameW(hMod, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0) return {};
  return std::filesystem::path(buf);
}
CG_ALWAYS_INLINE std::filesystem::path module_dir(HMODULE hMod = nullptr) {
  auto p = module_path(hMod);
  return p.empty() ? p : p.parent_path();
}
CG_ALWAYS_INLINE std::filesystem::path exe_path() { return module_path(nullptr); }
CG_ALWAYS_INLINE std::filesystem::path exe_dir()  { return module_dir(nullptr); }

// ---------- Unicode argv parsing ----------
CG_ALWAYS_INLINE std::vector<std::wstring> argv_wide() {
  int argc = 0;
  LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  std::vector<std::wstring> out;
  if (!argv) return out;
  out.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
  ::LocalFree(argv);
  return out;
}
CG_ALWAYS_INLINE std::vector<std::string> argv_utf8() {
  auto w = argv_wide();
  std::vector<std::string> out; out.reserve(w.size());
  for (auto& s : w) out.emplace_back(narrow(s));
  return out;
}

// ---------- High-resolution timing ----------
class Stopwatch {
public:
  Stopwatch() { reset(); }
  void   reset() {
    LARGE_INTEGER f{}; ::QueryPerformanceFrequency(&f); freq_ = f.QuadPart;
    LARGE_INTEGER s{}; ::QueryPerformanceCounter(&s);   start_ = s.QuadPart;
  }
  double  elapsed_sec() const {
    LARGE_INTEGER now{}; ::QueryPerformanceCounter(&now);
    return double(now.QuadPart - start_) / double(freq_);
  }
  int64_t elapsed_ms() const { return static_cast<int64_t>(elapsed_sec() * 1000.0); }
private:
  int64_t freq_{ 1 };
  int64_t start_{ 0 };
};

class ScopedTimer {
public:
  explicit ScopedTimer(const wchar_t* label) : label_(label) {}
  ~ScopedTimer() {
#ifdef _DEBUG
    const auto ms = sw_.elapsed_ms();
    wchar_t buf[256]{};
    _snwprintf_s(buf, _TRUNCATE, L"[TIMER] %s: %lld ms\n", label_ ? label_ : L"(unnamed)", static_cast<long long>(ms));
    ::OutputDebugStringW(buf);
#endif
  }
private:
  const wchar_t* label_{};
  Stopwatch      sw_{};
};

// ---------- Timer resolution RAII (winmm: timeBeginPeriod/timeEndPeriod) ----------
class ScopedTimerResolution {
public:
  explicit ScopedTimerResolution(UINT ms) : ms_(ms) { ::timeBeginPeriod(ms_); }
  ~ScopedTimerResolution() { ::timeEndPeriod(ms_); }
  CG_NO_COPY_MOVE(ScopedTimerResolution);
private:
  UINT ms_{1};
};

// ---------- High-precision sleep using waitable timers ----------
class HighPrecisionSleeper {
public:
  HighPrecisionSleeper() { h_.reset(::CreateWaitableTimerW(nullptr, TRUE, nullptr)); }
  bool valid() const noexcept { return !!h_; }
  // Sleep for given microseconds using negative relative due time (100-ns ticks)
  bool sleep_us(int64_t usec) {
    if (!h_) return false;
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(usec) * 10; // 1us = 10 * 100ns
    return ::SetWaitableTimer(h_.get(), &due, 0, nullptr, nullptr, FALSE) && (::WaitForSingleObject(h_.get(), INFINITE) == WAIT_OBJECT_0);
  }
private:
  Handle h_{};
};

// ---------- Sleep helpers ----------
CG_ALWAYS_INLINE void sleep_ms(uint32_t ms) { ::Sleep(ms); }
CG_ALWAYS_INLINE void yield() { ::Sleep(0); }

// ---------- Environment helpers ----------
CG_ALWAYS_INLINE std::optional<std::wstring> env_w(const wchar_t* name) {
  DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
  if (needed == 0) return std::nullopt;
  std::wstring v(needed, L'\0');
  DWORD got = ::GetEnvironmentVariableW(name, v.data(), needed);
  if (got == 0) return std::nullopt;
  if (!v.empty() && v.back() == L'\0') v.pop_back();
  return v;
}
CG_ALWAYS_INLINE bool set_env_w(const wchar_t* name, const wchar_t* value) {
  return !!::SetEnvironmentVariableW(name, value);
}

// ---------- System info ----------
CG_ALWAYS_INLINE uint32_t logical_processor_count() {
  return ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}
struct MemoryStatus {
  uint64_t total_phys{};
  uint64_t avail_phys{};
  uint64_t total_virt{};
  uint64_t avail_virt{};
};
CG_ALWAYS_INLINE MemoryStatus memory_status() {
  MEMORYSTATUSEX ms{ sizeof(ms) };
  ::GlobalMemoryStatusEx(&ms);
  return MemoryStatus{ ms.ullTotalPhys, ms.ullAvailPhys, ms.ullTotalVirtual, ms.ullAvailVirtual };
}

// ---------- Process / thread priority + RAII ----------
class ScopedProcessPriority {
public:
  explicit ScopedProcessPriority(DWORD klass) : old_(::GetPriorityClass(::GetCurrentProcess())) {
    ::SetPriorityClass(::GetCurrentProcess(), klass);
  }
  ~ScopedProcessPriority() { ::SetPriorityClass(::GetCurrentProcess(), old_); }
  CG_NO_COPY_MOVE(ScopedProcessPriority);
private:
  DWORD old_{NORMAL_PRIORITY_CLASS};
};
class ScopedThreadPriority {
public:
  explicit ScopedThreadPriority(int pri) : old_(::GetThreadPriority(::GetCurrentThread())) {
    ::SetThreadPriority(::GetCurrentThread(), pri);
  }
  ~ScopedThreadPriority() { ::SetThreadPriority(::GetCurrentThread(), old_); }
  CG_NO_COPY_MOVE(ScopedThreadPriority);
private:
  int old_{THREAD_PRIORITY_NORMAL};
};

// ---------- Thread naming (SetThreadDescription) ----------
CG_ALWAYS_INLINE bool set_thread_description(HANDLE thread, std::wstring_view name) {
  using Fn = HRESULT (WINAPI *)(HANDLE, PCWSTR);
  static Fn pSetThreadDescription = []() -> Fn {
    if (HMODULE k32 = ::GetModuleHandleW(L"Kernel32.dll")) {
      if (auto p = reinterpret_cast<Fn>(::GetProcAddress(k32, "SetThreadDescription"))) return p;
    }
    if (HMODULE kb = ::GetModuleHandleW(L"KernelBase.dll")) {
      if (auto p = reinterpret_cast<Fn>(::GetProcAddress(kb, "SetThreadDescription"))) return p;
    }
    return nullptr;
  }();
  if (!pSetThreadDescription) return false;
  return SUCCEEDED(pSetThreadDescription(thread, name.data()));
}
CG_ALWAYS_INLINE bool set_current_thread_description(std::wstring_view name) { return set_thread_description(::GetCurrentThread(), name); }
CG_ALWAYS_INLINE bool set_current_thread_description_utf8(std::string_view name) { return set_current_thread_description(widen(name)); }

// ---------- DPI awareness (runtime fallback; manifest preferred) ----------
enum class DpiSetup : uint8_t { NoChange, SystemAware, PerMonitorV2 };
CG_ALWAYS_INLINE bool set_process_dpi_awareness(DpiSetup mode) {
  if (mode == DpiSetup::NoChange) return true;
  using FnSetCtx = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
  static FnSetCtx pSetCtx = []() -> FnSetCtx {
    if (HMODULE user = ::GetModuleHandleW(L"user32.dll"))
      return reinterpret_cast<FnSetCtx>(::GetProcAddress(user, "SetProcessDpiAwarenessContext"));
    return nullptr;
  }();
  if (pSetCtx) {
    if (mode == DpiSetup::PerMonitorV2) return !!pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (mode == DpiSetup::SystemAware)  return !!pSetCtx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }
  using FnOld = BOOL (WINAPI*)();
  static FnOld pOld = []() -> FnOld {
    if (HMODULE user = ::GetModuleHandleW(L"user32.dll"))
      return reinterpret_cast<FnOld>(::GetProcAddress(user, "SetProcessDPIAware"));
    return nullptr;
  }();
  if (mode != DpiSetup::NoChange && pOld) return !!pOld();
  return false;
}

// ---------- Console attach (UTF‑8 codepage + VT) ----------
CG_ALWAYS_INLINE bool attach_parent_console_utf8(bool enableVT = true) {
  if (!::AttachConsole(ATTACH_PARENT_PROCESS)) return false;
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
  if (enableVT) {
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut && ::GetConsoleMode(hOut, &mode))
      ::SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
  return true;
}

// ---------- Clipboard (Unicode text) ----------
CG_ALWAYS_INLINE bool clipboard_set_text(std::wstring_view text) {
  if (!::OpenClipboard(nullptr)) return false;
  CG_DEFER { ::CloseClipboard(); });
  if (!::EmptyClipboard()) return false;
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL hglb = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!hglb) return false;
  void* mem = ::GlobalLock(hglb);
  std::memcpy(mem, text.data(), bytes);
  ::GlobalUnlock(hglb);
  return ::SetClipboardData(CF_UNICODETEXT, hglb) != nullptr;
}
CG_ALWAYS_INLINE std::optional<std::wstring> clipboard_get_text() {
  if (!::OpenClipboard(nullptr)) return std::nullopt;
  CG_DEFER { ::CloseClipboard(); });
  HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
  if (!h) return std::nullopt;
  const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
  if (!p) return std::nullopt;
  std::wstring out(p);
  ::GlobalUnlock(h);
  return out;
}

// ---------- Registry helpers (HKCU/HKLM) ----------
CG_ALWAYS_INLINE std::optional<std::wstring>
reg_read_sz(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
  DWORD type = 0, size = 0;
  LONG r = ::RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, &type, nullptr, &size);
  if (r != ERROR_SUCCESS || size == 0) return std::nullopt;
  std::wstring val(size / sizeof(wchar_t), L'\0');
  r = ::RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, &type, val.data(), &size);
  if (r != ERROR_SUCCESS) return std::nullopt;
  if (!val.empty() && val.back() == L'\0') val.pop_back();
  return val;
}
CG_ALWAYS_INLINE bool
reg_write_sz(HKEY root, const wchar_t* subkey, const wchar_t* valueName, std::wstring_view value) {
  return ::RegSetKeyValueW(root, subkey, valueName, REG_SZ, value.data(),
                           DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
CG_ALWAYS_INLINE std::optional<DWORD>
reg_read_dword(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
  DWORD type = 0, data = 0, size = sizeof(DWORD);
  LONG r = ::RegGetValueW(root, subkey, valueName, RRF_RT_REG_DWORD, &type, &data, &size);
  if (r != ERROR_SUCCESS) return std::nullopt;
  return data;
}
CG_ALWAYS_INLINE bool
reg_write_dword(HKEY root, const wchar_t* subkey, const wchar_t* valueName, DWORD value) {
  return ::RegSetKeyValueW(root, subkey, valueName, REG_DWORD, &value, sizeof(DWORD)) == ERROR_SUCCESS;
}

// ---------- Paths: temp dir & unique filename ----------
CG_ALWAYS_INLINE std::filesystem::path temp_dir() {
  wchar_t buf[MAX_PATH]{};
  DWORD n = ::GetTempPathW(DWORD(std::size(buf)), buf);
  if (n == 0 || n > std::size(buf)) return std::filesystem::temp_directory_path();
  return std::filesystem::path(buf);
}
CG_ALWAYS_INLINE std::filesystem::path unique_temp_file(std::wstring_view prefix = L"CG") {
  wchar_t dir[MAX_PATH]{}, name[MAX_PATH]{};
  DWORD n = ::GetTempPathW(DWORD(std::size(dir)), dir);
  if (!n || n > std::size(dir)) return temp_dir() / L"cg_temp.tmp";
  if (::GetTempFileNameW(dir, std::wstring(prefix).c_str(), 0, name) == 0)
    return std::filesystem::path(dir) / L"cg_temp.tmp";
  return std::filesystem::path(name);
}

// ---------- File I/O helpers ----------
namespace fs {

// Atomic replace: newFile -> dst (same volume). Preserves metadata when using ReplaceFileW.
CG_ALWAYS_INLINE bool replace_file_atomic(const std::filesystem::path& dst,
                                          const std::filesystem::path& newFile,
                                          std::string* err = nullptr) {
  auto d = ensure_long_path(dst);
  auto n = ensure_long_path(newFile);
  if (::ReplaceFileW(d.c_str(), n.c_str(), nullptr, REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr))
    return true;
  if (::MoveFileExW(n.c_str(), d.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  if (err) *err = last_error_u8();
  return false;
}

// Read entire small file to string (binary-safe)
CG_ALWAYS_INLINE bool read_all(const std::filesystem::path& file, std::string& out, std::string* err = nullptr) {
  Handle h(::CreateFileW(ensure_long_path(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!h) { if (err) *err = last_error_u8(); return false; }
  LARGE_INTEGER sz{}; if (!::GetFileSizeEx(h.get(), &sz)) { if (err) *err = last_error_u8(); return false; }
  out.resize(static_cast<size_t>(sz.QuadPart));
  DWORD rd = 0;
  if (!out.empty()) {
    if (!::ReadFile(h.get(), out.data(), static_cast<DWORD>(out.size()), &rd, nullptr) || rd != out.size()) {
      if (err) *err = last_error_u8(); return false;
    }
  }
  return true;
}

// Write to temp then atomically replace
CG_ALWAYS_INLINE bool write_atomic(const std::filesystem::path& dst, std::string_view data,
                                   bool writeThrough, std::string* err = nullptr) {
  auto dir = dst.parent_path().empty() ? std::filesystem::current_path() : dst.parent_path();
  auto tmp = dir / (dst.filename().wstring() + L".tmp~");
  DWORD flags = FILE_ATTRIBUTE_TEMPORARY | (writeThrough ? FILE_FLAG_WRITE_THROUGH : 0);
  Handle h(::CreateFileW(ensure_long_path(tmp).c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, flags, nullptr));
  if (!h) { if (err) *err = last_error_u8(); return false; }
  DWORD wr = 0;
  if (!data.empty()) {
    if (!::WriteFile(h.get(), data.data(), static_cast<DWORD>(data.size()), &wr, nullptr) || wr != data.size()) {
      if (err) *err = last_error_u8(); return false;
    }
  }
  ::FlushFileBuffers(h.get());
  h.reset();
  return replace_file_atomic(dst, tmp, err);
}

// Memory-mapped file (read-only)
class MappedFile {
public:
  MappedFile() = default;
  explicit MappedFile(const std::filesystem::path& p) { open(p); }
  ~MappedFile() { close(); }
  CG_NO_COPY_MOVE(MappedFile);

  bool open(const std::filesystem::path& p) {
    close();
    hFile_.reset(::CreateFileW(ensure_long_path(p).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hFile_) return false;
    LARGE_INTEGER sz{}; if (!::GetFileSizeEx(hFile_.get(), &sz)) return false; size_ = size_t(sz.QuadPart);
    hMap_.reset(::CreateFileMappingW(hFile_.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!hMap_) return false;
    view_ = ::MapViewOfFile(hMap_.get(), FILE_MAP_READ, 0, 0, 0);
    return view_ != nullptr;
  }

  void close() {
    if (view_) { ::UnmapViewOfFile(view_); view_ = nullptr; }
    hMap_.reset(); hFile_.reset(); size_ = 0;
  }

  const void* data() const noexcept { return view_; }
  size_t size() const noexcept { return size_; }
  bool   valid() const noexcept { return view_ != nullptr; }

private:
  Handle hFile_{}, hMap_{};
  void*  view_{}; size_t size_{};
};

} // namespace fs

// ---------- Timestamps ----------
CG_ALWAYS_INLINE std::wstring timestamp_utc_compact() {
  SYSTEMTIME st{}; ::GetSystemTime(&st);
  wchar_t buf[32]{};
  _snwprintf_s(buf, _TRUNCATE, L"%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return buf;
}
// ISO-8601 UTC (YYYY-MM-DDTHH:MM:SSZ)
CG_ALWAYS_INLINE std::wstring timestamp_iso8601_utc() {
  SYSTEMTIME st{}; ::GetSystemTime(&st);
  wchar_t buf[32]{};
  _snwprintf_s(buf, _TRUNCATE, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return buf;
}

// ---------- MiniDump writer (DbgHelp) ----------
#if CG_PCH_ENABLE_DBGHELP
CG_ALWAYS_INLINE bool write_minidump(const std::filesystem::path& dst,
                                     MINIDUMP_TYPE type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                                     EXCEPTION_POINTERS* ep = nullptr) {
  HANDLE hFile = ::CreateFileW(ensure_long_path(dst).c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  MINIDUMP_EXCEPTION_INFORMATION mei{};
  if (ep) { mei.ThreadId = ::GetCurrentThreadId(); mei.ExceptionPointers = ep; mei.ClientPointers = FALSE; }
  BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
                                hFile, type, ep ? &mei : nullptr, nullptr, nullptr);
  ::CloseHandle(hFile);
  return ok == TRUE;
}
#endif

// ---------- Optional stack trace (DbgHelp) ----------
#if CG_PCH_ENABLE_STACKTRACE && CG_PCH_ENABLE_DBGHELP
struct StackFrame { void* addr{}; std::string symbol; std::string file; uint32_t line{}; };

CG_ALWAYS_INLINE std::vector<void*> capture_stack_raw(ULONG framesToSkip = 1, ULONG framesToCapture = 32) {
  std::vector<void*> frames(framesToCapture);
  USHORT got = ::CaptureStackBackTrace(framesToSkip, framesToCapture, frames.data(), nullptr);
  frames.resize(got);
  return frames;
}
CG_ALWAYS_INLINE std::vector<StackFrame> resolve_symbols(const std::vector<void*>& addrs) {
  std::vector<StackFrame> out; if (addrs.empty()) return out;
  HANDLE process = ::GetCurrentProcess();
  static std::once_flag symOnce;
  std::call_once(symOnce, [&](){
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    ::SymInitialize(process, nullptr, TRUE);
  });
  struct SYM : SYMBOL_INFO { char buffer[512]; };
  SYM sym{}; sym.MaxNameLen = sizeof(sym.buffer); sym.SizeOfStruct = sizeof(SYMBOL_INFO);
  for (auto a : addrs) {
    StackFrame f{}; f.addr = a;
    DWORD64 displacement = 0;
    if (::SymFromAddr(process, DWORD64(a), &displacement, &sym)) {
      f.symbol.assign(sym.Name, sym.NameLen);
    }
    IMAGEHLP_LINEW64 line{}; line.SizeOfStruct = sizeof(line);
    DWORD dwDisp = 0;
    if (::SymGetLineFromAddrW64(process, DWORD64(a), &dwDisp, &line)) {
      f.file  = narrow(line.FileName);
      f.line  = line.LineNumber;
    }
    out.emplace_back(std::move(f));
  }
  return out;
}
#endif // stacktrace

// ---------- DXGI helpers ----------
#if CG_PCH_ENABLE_D3D11
CG_ALWAYS_INLINE bool dxgi_tearing_supported() {
  BOOL allow = FALSE;
  ComPtr<IDXGIFactory5> f5;
  if (SUCCEEDED(::CreateDXGIFactory1(IID_PPV_ARGS(f5.GetAddressOf())))) {
    if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
      return allow == TRUE;
  }
  return false;
}
CG_ALWAYS_INLINE void d3d11_enable_debug_breaks(ID3D11Device* device) {
#ifdef _DEBUG
  if (!device) return;
  ComPtr<ID3D11Debug> dbg;
  if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dbg.GetAddressOf())))) {
    ComPtr<ID3D11InfoQueue> q;
    if (SUCCEEDED(dbg.As(&q)) && q) {
      q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
      q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR,      TRUE);
    }
  }
#endif
}
#endif

// ---------- XAudio2 debug helper ----------
#if CG_PCH_ENABLE_XAUDIO2 && CG_HAS_XAUDIO2
CG_ALWAYS_INLINE void xaudio2_set_debug(IXAudio2* xa, bool enable) {
#ifdef _DEBUG
  if (!xa) return;
  XAUDIO2_DEBUG_CONFIGURATION cfg{};
  cfg.TraceMask = enable ? (XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS) : 0;
  cfg.BreakMask = 0;
  xa->SetDebugConfiguration(&cfg);
#else
  (void)xa; (void)enable;
#endif
}
#endif

// ---------- HLSL compile helper (D3DCompile) ----------
#if CG_PCH_ENABLE_HLSL_COMPILE && CG_PCH_ENABLE_D3D11 && __has_include(<d3dcompiler.h>)
CG_ALWAYS_INLINE HRESULT hlsl_compile(const void* src, size_t len,
                                      const char* entry, const char* target,
                                      const D3D_SHADER_MACRO* defines,
                                      ID3DInclude* inc, UINT flags,
                                      ComPtr<ID3DBlob>& outBlob,
                                      ComPtr<ID3DBlob>* outErrors = nullptr) {
  UINT fullFlags = flags;
#ifdef _DEBUG
  fullFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  fullFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
  ComPtr<ID3DBlob> code, errors;
  HRESULT hr = ::D3DCompile(src, len, nullptr, defines, inc, entry, target,
                            fullFlags, 0, code.GetAddressOf(), errors.GetAddressOf());
  if (outErrors) *outErrors = errors;
  if (SUCCEEDED(hr)) outBlob = code;
  return hr;
}
#endif

// ---------- Single instance guard (Named mutex) ----------
class SingleInstance {
public:
  explicit SingleInstance(std::wstring_view name) {
    name_ = L"Global\\" + std::wstring(name);
    h_.reset(::CreateMutexW(nullptr, FALSE, name_.c_str()));
    existed_ = (::GetLastError() == ERROR_ALREADY_EXISTS);
  }
  bool already_running() const noexcept { return existed_; }
  explicit operator bool() const noexcept { return !!h_; }
private:
  Handle h_{};
  bool existed_{false};
  std::wstring name_;
};

} // namespace cg::win
