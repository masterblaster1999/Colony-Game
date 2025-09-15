// platform/win/ProcessBootWin.h
//
// Windows bootstrap + platform utilities for Colony-Game (enhanced).
// - Centralizes early process setup (CWD, DPI, DLL search policy, error modes, etc.)
// - Adds robust, opt‑in features: COM init, process/thread priority, MMCSS (game/audio),
//   minidumps + crash handler, timer resolution, power throttling control, WER UI toggle,
//   console helpers (attach, VT, QuickEdit), single‑instance guard + window activation,
//   known-folder helpers, path utils, UTF‑8 conversions, OS capability probes,
//   Restart Manager registration, saved games dir provisioning, GPU preference hints,
//   process mitigations (safe subset), and more.
// - All functions are best‑effort and noexcept; failures degrade gracefully.
// - Implementation is expected in ProcessBootWin.cpp using dynamic API loading
//   (no new static link deps) and careful early‑process semantics.
//
// Quick start (defaults):
//   #include "platform/win/ProcessBootWin.h"
//   int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
//       ProcessBootWin::EarlyProcessInit(); // safe defaults
//       // ... your launcher ...
//   }
//
// Custom start:
//   ProcessBootWin::BootConfig cfg;
//   cfg.app_user_model_id = L"com.masterblaster1999.ColonyGame";
//   cfg.enable_minidumps = true;
//   cfg.set_timer_resolution_1ms = true;
//   cfg.set_process_priority = true;
//   cfg.process_priority = ProcessBootWin::ProcessPriority::kAboveNormal;
//   cfg.prevent_sleep_while_running = true;
//   ProcessBootWin::EarlyProcessInit(cfg);
//
// GPU preference export (define in exactly ONE .cpp TU before including this header there):
//   #define PROCESSBOOTWIN_DEFINE_GPU_PREFERENCE_EXPORTS
//   #include "platform/win/ProcessBootWin.h"
//
// NOTE: Keep this header Windows‑only.

#pragma once

#if !defined(_WIN32)
#  error "ProcessBootWin is Windows-only. Guard includes with #if defined(_WIN32)."
#endif

// Keep Windows headers lean and avoid std::min/max collisions.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

#if defined(PROCESSBOOTWIN_DEFINE_GPU_PREFERENCE_EXPORTS)
// Define vendor GPU preference exports in exactly one TU.
// These hints encourage NV/AMD switchable systems to pick the discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;        // NVIDIA
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1; // AMD
}
#endif

namespace ProcessBootWin {

// -----------------------------------------------------------------------------
// Versioning
// -----------------------------------------------------------------------------
struct Version { int major, minor, patch; };
inline constexpr Version kVersion{2, 0, 0};

// -----------------------------------------------------------------------------
// Enums & small PODs
// -----------------------------------------------------------------------------
enum class DpiMode {
    kNone = 0,        // Do not change process DPI awareness
    kSystem,          // System DPI aware
    kPerMonitorV2     // Best for multi‑DPI setups (Win10+)
};

enum class MinidumpKind {
    kSmall = 0,       // MiniDumpNormal
    kWithDataSegs,    // + data segments
    kWithFullMemory   // Full memory (large)
};

enum class ProcessPriority {
    kIdle,
    kBelowNormal,
    kNormal,
    kAboveNormal,
    kHigh,
    kRealTime // Use with caution
};

enum class ThreadPriority {
    kIdle          = -15,
    kLowest        = -2,
    kBelowNormal   = -1,
    kNormal        =  0,
    kAboveNormal   =  1,
    kHighest       =  2,
    kTimeCritical  = 15
};

enum class IoPriority {
    kVeryLow = 0, // background
    kLow     = 1,
    kNormal  = 2,
    kHigh    = 3
};

enum class ComApartment {
    kNone = 0,
    kSTA,
    kMTA
};

enum class MmcssTask {
    // Common MMCSS task profiles (subset).
    kGames,       // "Games"
    kAudio,       // "Audio"
    kProAudio,    // "Pro Audio"
    kPlayback,    // "Playback"
    kCapture      // "Capture"
};

enum class ActivationMechanism {
    kNone = 0,           // No activation message
    kRegisteredMessage,  // RegisterWindowMessage (default)
    kCopyData            // WM_COPYDATA (payload = UTF-8 cmdline)
};

struct MonitorInfo {
    HMONITOR handle{};
    RECT     rect{};
    RECT     work{};
    int      dpi_x{96};
    int      dpi_y{96};
    bool     primary{false};
};

struct MemoryStatus {
    std::uint64_t total_physical{};
    std::uint64_t avail_physical{};
    std::uint64_t total_virtual{};
    std::uint64_t avail_virtual{};
};

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
struct BootConfig {
    // Working directory
    bool set_working_directory_to_exe = true;
    std::wstring working_directory_override;   // If non-empty, must be absolute.

    // DPI Awareness
    bool enable_dpi_awareness = true;
    DpiMode dpi_mode = DpiMode::kPerMonitorV2;

    // DLL search policy
    bool harden_dll_search_path = true;        // Remove CWD from search; prefer System32 & user dirs

    // Error modes
    bool set_sane_error_modes = true;          // SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX
    unsigned custom_error_mode_flags = 0;      // If nonzero, overrides defaults

    // Heap safety
    bool enable_heap_termination_on_corruption = true;

    // AppUserModelID (taskbar grouping, notifications)
    bool set_app_user_model_id = true;
    std::wstring app_user_model_id = L"com.masterblaster1999.ColonyGame";

    // Console helpers
    bool attach_parent_console = false;        // Attach to parent console if any
    bool attach_console_when_debugger_present = true;
    bool redirect_stdio_to_console = true;
    bool enable_console_virtual_terminal = true; // Enable ANSI/VT sequences
    bool disable_console_quick_edit = true;      // Prevent accidental pause

    // COM initialization
    bool com_initialize = false;
    ComApartment com_apartment = ComApartment::kMTA;

    // Minidumps + crash handler
    bool enable_minidumps = false;
    std::wstring dump_folder = L"crashdumps";
    MinidumpKind dump_kind = MinidumpKind::kSmall;
    std::size_t max_dump_count = 8;
    bool install_unhandled_exception_handler = true; // writes dump on crash
    bool install_vectored_exception_handler = true;  // catches early SEH
    bool include_process_memory_in_dump = false;     // large; overrides kind to full memory if true

    // Timer resolution
    bool set_timer_resolution_1ms = false;     // restored on shutdown

    // Process priority & power
    bool set_process_priority = false;
    ProcessPriority process_priority = ProcessPriority::kAboveNormal;
    bool disable_power_throttling = true;      // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    bool prevent_sleep_while_running = false;  // ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED

    // Thread defaults (applied to calling thread only, typically main)
    bool set_main_thread_priority = false;
    ThreadPriority main_thread_priority = ThreadPriority::kAboveNormal;
    bool set_main_thread_io_priority = false;
    IoPriority main_thread_io_priority = IoPriority::kHigh;
    bool register_main_thread_mmcss = false;
    MmcssTask main_thread_mmcss_task = MmcssTask::kGames;

    // Single instance (opt-in to avoid conflicts with existing repo logic)
    bool ensure_single_instance = false;
    std::wstring single_instance_mutex_name = L"Global\\ColonyGame_SingleInstance_Mutex";
    ActivationMechanism activation_mechanism = ActivationMechanism::kRegisteredMessage;
    std::wstring activation_window_class;      // If known, helps find the right window
    bool bring_existing_window_to_front = true;

    // Windows Error Reporting
    bool disable_wer_ui = false;

    // Restart Manager (on unexpected exit, updates, etc.)
    bool register_application_restart = false; // RegisterApplicationRestart
    std::wstring restart_cmdline;              // If empty, uses current cmdline
    DWORD restart_flags = 0;                   // RESTART_* flags

    // Saved games directory provisioning (per-user)
    bool ensure_saved_games_subdir = false;    // Creates %USERPROFILE%\Saved Games\<saved_games_subdir>
    std::wstring saved_games_subdir = L"Colony Game";

    // Process mitigations (safe subset; no JIT-hostile settings)
    bool apply_safe_mitigations = false;       // ExtensionPointDisable, ImageLoadNoRemote/NoLowLabel, etc.

    // App icon & title tweaks (best-effort; can be left default)
    std::wstring console_title;                // If non-empty and console attached, set title
};

// -----------------------------------------------------------------------------
// Core boot API
// -----------------------------------------------------------------------------

// Minimal, safe initialization with opinionated defaults.
// Non-throwing; failed steps degrade gracefully.
void EarlyProcessInit() noexcept;

// Customized initialization with BootConfig.
// Non-throwing; failed steps degrade gracefully.
void EarlyProcessInit(const BootConfig& cfg) noexcept;

// One-liner convenience macro for call sites.
#define PROCESSBOOTWIN_EARLY_INIT() ::ProcessBootWin::EarlyProcessInit()

// RAII scope that:
//  - runs EarlyProcessInit(defaults) or with cfg in ctor
//  - tracks & restores timer resolution, power requests
//  - releases single-instance lock (if acquired), unregisters crash handlers
class BootScope final {
public:
    BootScope() noexcept;
    explicit BootScope(const BootConfig& cfg) noexcept;
    ~BootScope() noexcept;

    BootScope(const BootScope&) = delete;
    BootScope& operator=(const BootScope&) = delete;

    BootScope(BootScope&&) noexcept;
    BootScope& operator=(BootScope&&) noexcept;

private:
    void* _impl = nullptr; // pimpl; internal state for RAII releases
};

// -----------------------------------------------------------------------------
// Fine-grained building blocks (callable individually)
// -----------------------------------------------------------------------------

// --- Working directory / paths ---
void EnsureWorkingDirectory() noexcept;                         // Sets CWD to EXE dir
void EnsureWorkingDirectory(const wchar_t* absolute_dir) noexcept;
std::wstring GetExePathW() noexcept;                            // Full path to executable
std::wstring GetExeDirW() noexcept;                             // Directory of executable
std::wstring JoinPathW(const std::wstring& a,
                       const std::wstring& b) noexcept;
bool DirectoryExistsW(const wchar_t* path) noexcept;
bool EnsureDirectoryW(const wchar_t* path) noexcept;            // Creates (recursively) if missing
std::wstring ExpandEnvVarsW(const wchar_t* input) noexcept;     // Expands %VAR% and returns result
std::wstring NormalizePathW(const wchar_t* path) noexcept;      // Removes ./ ..\ and normalizes slashes
std::wstring MakeTimestampedFileNameW(const wchar_t* prefix,
                                      const wchar_t* ext) noexcept; // e.g., prefix_YYYYMMDD_HHMMSS.ext

// Known folders (best-effort; returns empty on failure)
enum class KnownFolder {
    kRoamingAppData,
    kLocalAppData,
    kSavedGames,
    kDocuments,
    kPictures
};
std::wstring GetKnownFolderPath(KnownFolder id) noexcept;

// Saved games helper
std::wstring GetOrCreateSavedGamesPath(const wchar_t* subdir) noexcept; // %USERPROFILE%\Saved Games\subdir

// --- UTF‑8 / Wide helpers ---
std::wstring Utf8ToWide(const char* utf8) noexcept;
std::string  WideToUtf8(const wchar_t* wide) noexcept;
std::vector<std::string> GetUtf8CommandLineArgs() noexcept; // robust UTF-16 → UTF-8 argv
std::string  GetUtf8CommandLine() noexcept;                 // entire command line as UTF-8

// --- DPI awareness ---
bool IsPerMonitorV2Available() noexcept;                        // Capability probe
bool SetDpiAwareness(DpiMode mode) noexcept;                    // Applies requested DPI mode
inline bool SetProcessDpiPerMonitorV2() noexcept { return SetDpiAwareness(DpiMode::kPerMonitorV2); }
inline bool SetProcessDpiSystemAware()  noexcept { return SetDpiAwareness(DpiMode::kSystem); }
inline bool SetDpiAwarenessNone()       noexcept { return SetDpiAwareness(DpiMode::kNone); }

// Monitor enumeration (best-effort; DPI via GetDpiForMonitor if available)
std::vector<MonitorInfo> EnumerateMonitors() noexcept;

// --- DLL search hardening ---
void HardenDllSearchPath() noexcept;                             // Removes CWD from search; prefers System32 & user dirs

// --- Error modes / heap safety ---
void SetSaneErrorModes(unsigned flags = 0) noexcept;             // If flags==0, uses sensible defaults
void EnableHeapTerminationOnCorruption() noexcept;

// --- AppUserModelID ---
void SetAppUserModelID(const wchar_t* app_id) noexcept;          // Ignores failures on older OSes

// --- Console helpers ---
bool AttachConsoleForDebug(bool only_if_debugger_present = true,
                           bool redirect_stdio = true) noexcept;
bool AttachParentConsole(bool redirect_stdio = true) noexcept;
bool EnableConsoleVirtualTerminal() noexcept;                     // Enables ANSI colors, etc.
bool DisableConsoleQuickEdit() noexcept;                          // Prevents pause on accidental selection
void SetConsoleTitleW(const wchar_t* title) noexcept;

// --- COM ---
class CoInitScope {
public:
    CoInitScope() noexcept = default;
    explicit CoInitScope(ComApartment apt) noexcept; // calls CoInitializeEx; no-op on kNone
    ~CoInitScope() noexcept;                         // CoUninitialize if needed

    CoInitScope(const CoInitScope&) = delete;
    CoInitScope& operator=(const CoInitScope&) = delete;

    CoInitScope(CoInitScope&&) noexcept;
    CoInitScope& operator=(CoInitScope&&) noexcept;

private:
    bool _inited = false;
};

// --- Timer resolution ---
bool RequestTimerResolution1ms() noexcept;                       // Returns true if changed to 1ms
void RestoreTimerResolution() noexcept;                          // No-op if unchanged
class TimerResolutionScope {
public:
    TimerResolutionScope() noexcept;
    ~TimerResolutionScope() noexcept;

    TimerResolutionScope(const TimerResolutionScope&) = delete;
    TimerResolutionScope& operator=(const TimerResolutionScope&) = delete;

    TimerResolutionScope(TimerResolutionScope&&) noexcept;
    TimerResolutionScope& operator=(TimerResolutionScope&&) noexcept;

private:
    bool _active{false};
};

// --- Power / sleep ---
class AwakeScope { // Prevents system/display sleep while alive
public:
    AwakeScope(bool keep_display_awake = true) noexcept;
    ~AwakeScope() noexcept;

    AwakeScope(const AwakeScope&) = delete;
    AwakeScope& operator=(const AwakeScope&) = delete;

    AwakeScope(AwakeScope&&) noexcept;
    AwakeScope& operator=(AwakeScope&&) noexcept;

private:
    EXECUTION_STATE _prev{};
};

bool DisablePowerThrottlingForProcess() noexcept;                // PROCESS_POWER_THROTTLING_EXECUTION_SPEED

// --- Process & thread priority ---
bool SetThisProcessPriority(ProcessPriority p) noexcept;
bool SetThisThreadPriority(ThreadPriority p) noexcept;
bool SetThisThreadIoPriority(IoPriority io) noexcept;

// --- MMCSS (multimedia scheduler) ---
class MmcssScope {
public:
    // Registers calling thread with MMCSS under given task profile (e.g., "Games").
    explicit MmcssScope(MmcssTask task, int thread_priority_hint = 0) noexcept;
    ~MmcssScope() noexcept;

    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;

    MmcssScope(MmcssScope&&) noexcept;
    MmcssScope& operator=(MmcssScope&&) noexcept;

private:
    void* _hTask = nullptr; // HANDLE from AvSetMmThreadCharacteristics
};

// --- Minidumps & crash handling ---
using CrashCallback = void(*)(EXCEPTION_POINTERS*);

bool EnableMinidumps(const wchar_t* dump_dir,
                     MinidumpKind kind = MinidumpKind::kSmall,
                     std::size_t max_dumps = 8,
                     bool include_full_memory = false) noexcept;

// Installs unhandled/vectored handlers that write dumps to configured folder.
// If cb is non-null, it will be invoked (best-effort) just before writing the dump.
bool InstallCrashHandlers(bool install_vectored = true,
                          CrashCallback cb = nullptr) noexcept;

// Removes previously installed handlers (best-effort).
void RemoveCrashHandlers() noexcept;

// Utility to programmatically trigger a test crash (guarded; no-op in release if desired).
void TriggerIntentionalCrash() noexcept;

// --- Single-instance guard + activation ---
class InstanceLock {
public:
    InstanceLock() noexcept = default;
    static InstanceLock Create(const wchar_t* mutex_name,
                               bool global_namespace = true) noexcept;

    bool IsPrimary() const noexcept;
    void Release() noexcept;

    // Attempts to bring existing instance to foreground.
    // If window_class_name provided, uses FindWindow on that class; otherwise a best-effort search.
    void BringExistingToFront(const wchar_t* window_class_name = nullptr) noexcept;

    HANDLE native_handle() const noexcept { return _h; }

    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    InstanceLock(InstanceLock&& other) noexcept;
    InstanceLock& operator=(InstanceLock&& other) noexcept;

    ~InstanceLock() noexcept;

private:
    explicit InstanceLock(HANDLE h) noexcept : _h(h) {}
    HANDLE _h = nullptr;
};

// Activation message helpers (pair with InstanceLock)
UINT GetActivationMessageId() noexcept; // RegisterWindowMessage(L"ProcessBootWin.Activate.<exe-path>")
bool PostActivateMessage(const wchar_t* window_class = nullptr) noexcept; // posts to main window if discoverable

// --- WER UI ---
void SetCrashDialogVisibility(bool enabled) noexcept; // Toggles WER UI where supported (best-effort)

// --- Restart Manager ---
bool RegisterApplicationRestart(const wchar_t* cmdline, DWORD flags) noexcept;
void UnregisterApplicationRestart() noexcept;

// --- Process mitigations (safe subset) ---
bool ApplySafeMitigations() noexcept; // ExtensionPointDisable, image load rules, etc.

// --- OS Version helpers (robust, not affected by manifest) ---
bool IsWindows7OrGreater() noexcept;
bool IsWindows8OrGreater() noexcept;
bool IsWindows10OrGreater() noexcept;
bool IsWindows11OrGreater() noexcept;

// --- System information ---
MemoryStatus GetSystemMemoryStatus() noexcept;
unsigned GetLogicalProcessorCount() noexcept;  // logical cores
unsigned GetPhysicalCoreCount() noexcept;      // physical cores (best-effort)

// --- Thread utilities ---
bool SetCurrentThreadDescriptionUtf8(const char* name_utf8) noexcept;

// --- GPU preference (runtime hints; vendor exports handled via macro above) ---
void SetPreferredGpuHighPerformance() noexcept; // Best-effort runtime hints (e.g., power throttling off)

// --- Shell helpers ---
bool OpenUrlOrFile(const wchar_t* path_or_url) noexcept; // ShellExecute best-effort
bool RevealInExplorer(const wchar_t* absolute_path) noexcept; // Selects file in Explorer if exists

// --- Minimal DllMain-safe pre-init (rare) ---
void MinimalProcessPreInit() noexcept;

// --- Diagnostics ---
std::wstring GetLastErrorAsWString(DWORD err = GetLastError()) noexcept;
bool IsProcessElevated() noexcept;
bool IsBeingDebugged() noexcept;
bool IsRunningUnderWine() noexcept; // heuristic via exported symbol "wine_get_version"

// -----------------------------------------------------------------------------
// Integration helpers & macros
// -----------------------------------------------------------------------------

// One-liner you can drop at the top of WinMain:
#define PROCESSBOOTWIN_CALL_EARLY_INIT() ::ProcessBootWin::EarlyProcessInit()

// Optional: define this macro in exactly ONE .cpp TU to export GPU preference hints.
//   #define PROCESSBOOTWIN_DEFINE_GPU_PREFERENCE_EXPORTS
//   #include "platform/win/ProcessBootWin.h"

// Notes for implementer (.cpp):
// - Use GetProcAddress for optional APIs (Shcore DPI, SetProcessDpiAwarenessContext,
//   AvSetMmThreadCharacteristicsW, SetProcessMitigationPolicy, etc.).
// - Avoid static linkage on dbghelp/winmm/avrt: load modules dynamically.
// - Ensure crash handlers are async-signal-safe enough to write a dump quickly.
// - Rotate dumps to 'max_dump_count' in 'dump_folder'; name via MakeTimestampedFileNameW().
// - For activation: prefer RegisterWindowMessage; fallback to WM_COPYDATA when requested.
// - For saved games: SHGetKnownFolderPath(FOLDERID_SavedGames), fallback to Documents.
// - For mitigations: choose a conservative set that won't break typical middleware.
// - All API must remain noexcept.
//
// This header intentionally exposes only declarations (plus a few tiny inline wrappers).
// Implementation should keep behavior conservative and fail-safe.

} // namespace ProcessBootWin
