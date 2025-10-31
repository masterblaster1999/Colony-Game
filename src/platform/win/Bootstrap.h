// src/platform/win/Bootstrap.h
//
// Windows-only process bootstrap for Colony-Game.
// Massively upgraded header: configurable initialization, single-instance guard,
// DPI awareness, console attach/allocate, COM init, crash dump writer, timer
// resolution bump, and detailed reporting — all header-only by default.
//
// To use (minimal):
//   #include "platform/win/Bootstrap.h"
//   int main() {
//     winboot::InitOptions opts; // tweak as needed
//     winboot::Context ctx;
//     if (!winboot::Initialize(opts, &ctx)) return 0; // exits if already running
//     // ... your game init / loop ...
//   }
//
// Notes:
//   • This header is Windows-only and uses C++17.
//   • By default, everything is implemented inline here (header-only).
//   • Libraries linked via #pragma comment: Dbghelp, Shell32, Ole32, Winmm.
//   • If you previously added a Bootstrap.cpp, either remove it or set
//     WINBOOT_NO_HEADER_IMPLEMENTATION in that TU to avoid ODR conflicts.
//
// MIT-0 style: Use as you like. No warranty.

#pragma once

#if !defined(_WIN32)
#  error "src/platform/win/Bootstrap.h is Windows-only."
#endif

// Keep Windows headers lean.
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

// ======== System & CRT includes ========
#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>   // SHGetKnownFolderPath, FOLDERID_*
#include <dbghelp.h>       // MiniDumpWriteDump
#include <mmsystem.h>      // timeBeginPeriod, timeEndPeriod
#include <cstdio>
#include <cwchar>
#include <string>
#include <string_view>
#include <filesystem>
#include <functional>
#include <optional>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <mutex>

#ifdef _MSC_VER
  #include <crtdbg.h>      // optional: _CrtSetReportMode
  #include <corecrt.h>     // _set_invalid_parameter_handler
#endif

// ======== Link required Win32 libs (MSVC) ========
#if defined(_MSC_VER)
  #pragma comment(lib, "Dbghelp.lib")
  #pragma comment(lib, "Shell32.lib")
  #pragma comment(lib, "Ole32.lib")
  #pragma comment(lib, "Winmm.lib")
#endif

// ======== DPI awareness context fallbacks (older SDKs) ========
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
  #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)-3)
#endif
#ifndef DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
  #define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((HANDLE)-2)
#endif
#ifndef DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
  #define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED ((HANDLE)-5)
#endif

// ======== Public API ========
namespace winboot {

// Version (for diagnostics / telemetry if you want)
constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 2;
constexpr int kVersionPatch = 0;

enum class LogLevel : uint8_t { Info, Warning, Error, Debug };

// Optional logger sink. If provided, bootstrap will emit short messages.
using LogSink = std::function<void(LogLevel, std::wstring_view)>;

enum class DpiAwareness : uint8_t {
  Unaware,
  System,          // System DPI aware
  PerMonitor,      // Per-monitor v1
  PerMonitorV2,    // Per-monitor v2 (best for modern Windows)
  UnawareGdiScaled // Unaware but GDI scaled by OS (legacy)
};

enum class InstancePolicy : uint8_t {
  None,            // Do not enforce single instance
  Enforce          // Show dialog and fail Initialize() if already running
};

enum class ComApartment : uint8_t {
  None,
  STA,
  MTA
};

enum class ConsoleMode : uint8_t {
  None,            // Do nothing
  Attach,          // Attach to parent console if present (no allocate)
  Allocate,        // Allocate a new console (shows a window)
  AllocateHidden   // Allocate then immediately hide the console window
};

enum class TimerResolution : uint8_t {
  None,
  Ms1
};

struct CrashHandlerOptions {
  bool        enable        = true;          // install SetUnhandledExceptionFilter
  bool        show_dialog   = true;          // show a MessageBox after writing dump
  std::wstring dump_directory;               // empty => %LOCALAPPDATA%\ColonyGame\crashdumps
  uint32_t    minidump_flags = 0;            // 0 => sensible defaults chosen at runtime
  std::wstring dump_file_prefix = L"ColonyGame";
};

struct InitOptions {
  // Branding (used in dialogs, dump prefix, etc.)
  std::wstring app_display_name = L"Colony-Game";

  // Single instance
  InstancePolicy instance_policy = InstancePolicy::Enforce;
  std::wstring   instance_mutex_name = L"ColonyGame_SingleInstance";

  // Process hygiene
  bool pin_working_directory_to_exe = true;
  bool suppress_os_error_dialogs    = true;

  // High DPI
  DpiAwareness dpi_awareness = DpiAwareness::PerMonitorV2;

  // COM (for shell APIs, etc.)
  ComApartment com = ComApartment::STA; // STA is safest default for many shell calls

  // Console
  ConsoleMode console = ConsoleMode::None;
  bool        utf8_console = true;      // SetConsoleCP/OutputCP to UTF-8 when a console is present

  // Timer resolution
  TimerResolution timer_resolution = TimerResolution::None;

  // Crash handler
  CrashHandlerOptions crash{};
};

// What happened during initialization.
struct InitReport {
  bool working_dir_pinned        = false;
  bool single_instance_acquired  = false;  // true if this is the first instance
  bool dpi_awareness_set         = false;
  bool com_initialized           = false;
  bool console_ready             = false;
  bool crash_handler_installed   = false;
  bool timer_resolution_bumped   = false;

  std::filesystem::path exe_path;
  std::filesystem::path working_dir;
  std::filesystem::path dump_directory;    // if crash handler enabled

  // Opaque OS handles (may be null if not used)
  void* instance_mutex           = nullptr;
};

// RAII context that tracks resources to clean up on destruction.
struct Context {
  InitReport report{};

  Context() = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  Context(Context&& other) noexcept { *this = std::move(other); }
  Context& operator=(Context&& other) noexcept {
    if (this != &other) {
      cleanup();
      report = other.report;
      other.report = {};
      other.report.instance_mutex = nullptr;
    }
    return *this;
  }

  ~Context() { cleanup(); }

private:
  void cleanup();
};

// --- Core bootstrap ---------------------------------------------------------

// Initialize the process according to 'options'.
// Returns 'false' ONLY when InstancePolicy::Enforce is set and another instance
// is already running. All other sub-steps try to succeed best-effort.
// If 'outCtx' is provided, it receives a RAII context so resources are released
// when it goes out of scope (mutex, COM, timer resolution, console, etc.).
[[nodiscard]]
bool Initialize(const InitOptions& options,
                Context* outCtx = nullptr,
                LogSink log = {});

// Convenience: Initialize with defaults.
[[nodiscard]]
inline bool EarlyInit() { return Initialize(InitOptions{}, nullptr, {}); }

// Back-compat with earlier versions (discouraged).
[[deprecated("Use Initialize(const InitOptions&, ...) instead.")]]
inline void EarlyInit(const wchar_t* mutexName) {
  InitOptions o;
  o.instance_mutex_name = mutexName ? mutexName : L"ColonyGame_SingleInstance";
  (void)Initialize(o, nullptr, {}); // explicit discard: resolves C4834
}

// --- Handy utilities --------------------------------------------------------
[[nodiscard]] std::filesystem::path ExePath();
[[nodiscard]] std::filesystem::path ExeDirectory();
[[nodiscard]] std::filesystem::path LocalAppDataDirectory();

// Best-effort: try to bring an existing instance to foreground (if you enforce single instance).
// Returns true if we believe we found & activated a window. Safe to call even if none exists.
bool BringExistingInstanceToFront(std::wstring_view likely_window_title = L"Colony-Game");

} // namespace winboot

// ===========================================================================
// ================== Inline Implementation (header-only) ====================
// ===========================================================================

#ifndef WINBOOT_NO_HEADER_IMPLEMENTATION
namespace winboot {
namespace detail {

using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);

// Global-ish state (C++17 inline variables to avoid ODR issues across TUs)
inline LogSink g_log{};
inline std::wstring g_appName = L"Colony-Game";

inline LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
inline CrashHandlerOptions g_crashOpts{};
inline std::filesystem::path g_dumpDir;

inline HANDLE g_instanceMutex = nullptr;
inline bool   g_consoleAllocated = false;
inline UINT   g_timerPeriodMs = 0;

inline void emit(LogLevel lvl, std::wstring_view msg) {
  if (g_log) g_log(lvl, msg);
}

inline std::wstring sanitize_for_filename(std::wstring s) {
  for (auto& ch : s) {
    switch (ch) {
      case L'\\': case L'/': case L':': case L'*': case L'?':
      case L'"': case L'<': case L'>': case L'|':
        ch = L'_'; break;
      default: break;
    }
  }
  if (s.empty()) s = L"ColonyGame";
  return s;
}

inline std::filesystem::path ensure_dir(std::filesystem::path p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

inline std::filesystem::path get_exe_path() {
  wchar_t buf[MAX_PATH] = {};
  DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return (n > 0) ? std::filesystem::path(buf) : std::filesystem::path{};
}

inline std::filesystem::path get_exe_dir() {
  auto p = get_exe_path();
  return p.empty() ? p : p.parent_path();
}

inline std::filesystem::path get_local_appdata() {
  PWSTR w = nullptr;
  std::filesystem::path out;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &w)) && w) {
    out = w;
    ::CoTaskMemFree(w);
  }
  return out;
}

inline void pin_working_dir_to_exe(InitReport& r) {
  auto dir = get_exe_dir();
  if (!dir.empty()) {
    std::error_code ec;
    std::filesystem::current_path(dir, ec);
    if (!ec) {
      r.working_dir_pinned = true;
      r.working_dir = dir;
    }
  }
}

inline void disable_os_error_dialogs() {
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _MSC_VER
  _invalid_parameter_handler h = [] (const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {};
  _set_invalid_parameter_handler(h);
  #ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_ASSERT, 0);
  #endif
#endif
}

inline bool set_dpi_awareness(DpiAwareness mode) {
  HMODULE user32 = ::LoadLibraryW(L"user32.dll");
  if (!user32) return false;
  auto setCtx = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
      ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
  BOOL ok = FALSE;
  if (setCtx) {
    HANDLE ctx = nullptr;
    switch (mode) {
      default:
      case DpiAwareness::PerMonitorV2:    ctx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2; break;
      case DpiAwareness::PerMonitor:      ctx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;    break;
      case DpiAwareness::System:          ctx = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;         break;
      case DpiAwareness::UnawareGdiScaled:ctx = DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED;    break;
      case DpiAwareness::Unaware:         ctx = nullptr;                                    break;
    }
    if (ctx) ok = setCtx(ctx);
  }
  if (!ok && mode != DpiAwareness::Unaware) {
    ok = ::SetProcessDPIAware(); // fallback: system-DPI aware
  }
  ::FreeLibrary(user32);
  return ok == TRUE;
}

inline bool acquire_single_instance(const std::wstring& name, InitReport& r, const std::wstring& appTitle, InstancePolicy policy) {
  HANDLE h = ::CreateMutexW(nullptr, TRUE, name.c_str());
  DWORD gle = ::GetLastError();
  if (h && gle == ERROR_ALREADY_EXISTS) {
    if (policy == InstancePolicy::Enforce) {
      ::MessageBoxW(nullptr,
        L"Colony-Game is already running.\n\nIf you cannot find it, check the taskbar or system tray.",
        appTitle.c_str(), MB_ICONINFORMATION | MB_OK);
      if (h) ::CloseHandle(h);
      return false;
    } else {
      // Not enforcing; just proceed without taking ownership.
      ::CloseHandle(h);
      detail::emit(LogLevel::Warning, L"Another instance is running (not enforced).");
      return true;
    }
  }
  r.single_instance_acquired = (h != nullptr);
  r.instance_mutex = h;
  detail::g_instanceMutex = h;
  return true;
}

// Console helpers
inline void redirect_std_handles_to_console() {
  FILE* fp = nullptr;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  freopen_s(&fp, "CONOUT$", "w", stderr);
  freopen_s(&fp, "CONIN$",  "r", stdin);
}

inline bool setup_console(ConsoleMode mode, bool utf8, InitReport& r) {
  if (mode == ConsoleMode::None) return false;

  bool attached_or_alloc = false;
  if (mode == ConsoleMode::Attach) {
    attached_or_alloc = (::AttachConsole(ATTACH_PARENT_PROCESS) != 0);
  } else {
    attached_or_alloc = (::AllocConsole() != 0);
    detail::g_consoleAllocated = attached_or_alloc;
  }

  if (attached_or_alloc) {
    redirect_std_handles_to_console();
    if (utf8) { ::SetConsoleCP(CP_UTF8); ::SetConsoleOutputCP(CP_UTF8); }
    if (mode == ConsoleMode::AllocateHidden) {
      ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
    }
    r.console_ready = true;
    return true;
  }
  return false;
}

// Timer resolution
inline bool bump_timer_resolution(TimerResolution tr) {
  if (tr == TimerResolution::Ms1) {
    if (::timeBeginPeriod(1) == TIMERR_NOERROR) {
      detail::g_timerPeriodMs = 1;
      return true;
    }
  }
  return false;
}

// Crash handler
inline uint32_t default_minidump_flags() {
  return static_cast<uint32_t>(
      MiniDumpWithIndirectlyReferencedMemory |
      MiniDumpScanMemory |
      MiniDumpWithThreadInfo |
      MiniDumpWithHandleData |
      MiniDumpWithDataSegs |
      MiniDumpWithUnloadedModules);
}

inline LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
  using namespace std;
  using namespace std::filesystem;

  SYSTEMTIME st; ::GetLocalTime(&st);

  wstring prefix = sanitize_for_filename(detail::g_crashOpts.dump_file_prefix.empty()
                                         ? detail::g_appName
                                         : detail::g_crashOpts.dump_file_prefix);
  wchar_t fname[256];
  swprintf_s(fname, L"%s_%04u%02u%02u-%02u%02u%02u.dmp",
             prefix.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

  path dir = detail::g_dumpDir;
  if (dir.empty()) dir = ensure_dir(get_local_appdata() / L"ColonyGame" / L"crashdumps");
  path full = dir / fname;

  HANDLE hFile = ::CreateFileW(full.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    MINIDUMP_TYPE type = (detail::g_crashOpts.minidump_flags != 0)
      ? static_cast<MINIDUMP_TYPE>(detail::g_crashOpts.minidump_flags)
      : static_cast<MINIDUMP_TYPE>(default_minidump_flags());

    BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
                                  hFile, type, &mei, nullptr, nullptr);
    ::CloseHandle(hFile);

    if (ok) {
      std::wstring msg = L"A crash report was saved to:\n" + full.wstring() +
                         L"\n\nPlease attach it when reporting the issue.";
      emit(LogLevel::Error, L"Crash dump written.");
      if (detail::g_crashOpts.show_dialog) {
        ::MessageBoxW(nullptr, msg.c_str(), detail::g_appName.c_str(), MB_ICONERROR | MB_OK);
      }
    } else {
      if (detail::g_crashOpts.show_dialog) {
        ::MessageBoxW(nullptr, L"Colony-Game crashed but the crash dump could not be saved.",
                      detail::g_appName.c_str(), MB_ICONERROR | MB_OK);
      }
    }
  } else {
    if (detail::g_crashOpts.show_dialog) {
      ::MessageBoxW(nullptr, L"Colony-Game crashed but could not create the dump file.",
                    detail::g_appName.c_str(), MB_ICONERROR | MB_OK);
    }
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

inline void install_crash_handler(const InitOptions& o, InitReport& r) {
  if (!o.crash.enable) return;
  detail::g_crashOpts = o.crash;
  detail::g_dumpDir = o.crash.dump_directory;
  detail::g_prevFilter = ::SetUnhandledExceptionFilter(unhandled_filter);
  r.crash_handler_installed = true;
}

} // namespace detail

// ---- Context cleanup ----
inline void Context::cleanup() {
  // Restore crash filter?
  if (report.crash_handler_installed) {
    ::SetUnhandledExceptionFilter(detail::g_prevFilter);
    report.crash_handler_installed = false;
  }
  // Timer resolution
  if (report.timer_resolution_bumped && detail::g_timerPeriodMs) {
    ::timeEndPeriod(detail::g_timerPeriodMs);
    report.timer_resolution_bumped = false;
    detail::g_timerPeriodMs = 0;
  }
  // COM
  if (report.com_initialized) {
    ::CoUninitialize();
    report.com_initialized = false;
  }
  // Single instance mutex
  if (report.instance_mutex) {
    ::CloseHandle(reinterpret_cast<HANDLE>(report.instance_mutex));
    report.instance_mutex = nullptr;
    report.single_instance_acquired = false;
    detail::g_instanceMutex = nullptr;
  }
  // Console: only free if we allocated one
  if (detail::g_consoleAllocated) {
    ::FreeConsole();
    detail::g_consoleAllocated = false;
    report.console_ready = false;
  }
}

// ---- Utilities ----
inline std::filesystem::path ExePath() { return detail::get_exe_path(); }
inline std::filesystem::path ExeDirectory() { return detail::get_exe_dir(); }
inline std::filesystem::path LocalAppDataDirectory() { return detail::get_local_appdata(); }

// Best-effort: find and activate a top-level window with a matching title.
// This is heuristic. For best results, pass your exact window title once you know it.
inline bool BringExistingInstanceToFront(std::wstring_view likely_window_title) {
  bool activated = false;
  ::EnumWindows([](HWND hwnd, LPARAM lParam)->BOOL {
      auto title_sv = reinterpret_cast<std::wstring_view*>(lParam);
      wchar_t buf[512]; buf[0] = 0;
      ::GetWindowTextW(hwnd, buf, 512);
      if (wcslen(buf) == 0) return TRUE;
      std::wstring_view t{buf};
      if (t.find(*title_sv) != std::wstring_view::npos) {
        ::ShowWindow(hwnd, SW_SHOWNORMAL);
        ::SetForegroundWindow(hwnd);
        return FALSE; // stop
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&likely_window_title));
  // EnumWindows can't pass activation result back easily; assume success if not cancelled.
  // This is just a helper; callers shouldn't rely on it strictly.
  (void)activated;
  return true;
}

// ---- Initialize ----
inline bool Initialize(const InitOptions& options, Context* outCtx, LogSink log) {
  detail::g_log = log;
  detail::g_appName = options.app_display_name.empty() ? L"Colony-Game" : options.app_display_name;

  Context local;
  InitReport& r = local.report;

  r.exe_path = ExePath();
  r.working_dir = ExeDirectory();

  // 1) Process hygiene
  if (options.suppress_os_error_dialogs) {
    detail::disable_os_error_dialogs();
  }
  if (options.pin_working_directory_to_exe) {
    detail::pin_working_dir_to_exe(r);
  }

  // 2) Single instance
  if (!detail::acquire_single_instance(options.instance_mutex_name, r, detail::g_appName, options.instance_policy)) {
    // Existing instance present; optionally try to bring it to front.
    BringExistingInstanceToFront(detail::g_appName);
    return false;
  }

  // 3) DPI awareness
  if (options.dpi_awareness != DpiAwareness::Unaware) {
    r.dpi_awareness_set = detail::set_dpi_awareness(options.dpi_awareness);
  }

  // 4) Console
  if (options.console != ConsoleMode::None) {
    (void)detail::setup_console(options.console, options.utf8_console, r);
  }

  // 5) COM
  if (options.com != ComApartment::None) {
    DWORD coflags = (options.com == ComApartment::STA)
                  ? (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)
                  : (COINIT_MULTITHREADED    | COINIT_DISABLE_OLE1DDE);
    HRESULT hr = ::CoInitializeEx(nullptr, coflags);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
      r.com_initialized = SUCCEEDED(hr);
      if (hr == RPC_E_CHANGED_MODE) {
        detail::emit(LogLevel::Warning, L"COM already initialized with a different apartment model.");
      }
    } else {
      detail::emit(LogLevel::Warning, L"CoInitializeEx failed; continuing.");
    }
  }

  // 6) Timer resolution
  if (options.timer_resolution == TimerResolution::Ms1) {
    r.timer_resolution_bumped = detail::bump_timer_resolution(options.timer_resolution);
  }

  // 7) Crash handler
  if (options.crash.enable) {
    r.dump_directory = options.crash.dump_directory.empty()
        ? (LocalAppDataDirectory() / L"ColonyGame" / L"crashdumps")
        : std::filesystem::path(options.crash.dump_directory);
    r.dump_directory = detail::ensure_dir(r.dump_directory);
    detail::install_crash_handler(options, r);
  }

  // Success. Hand off RAII context (move) to caller or keep local until return.
  if (outCtx) {
    *outCtx = std::move(local);
  } else {
    static Context s_ctx; // process-lifetime fallback to keep resources alive
    s_ctx = std::move(local);
  }
  return true;
}

} // namespace winboot
#endif // WINBOOT_NO_HEADER_IMPLEMENTATION
