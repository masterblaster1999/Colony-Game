// platform/win/LauncherSystemWin.cpp
//
// Windows-only process/system helpers used by WinLauncher.cpp.
// Provides the real implementations declared in platform/win/LauncherSystemWin.h.
//
// Key behaviors:
//  - Friendly error formatting (LastErrorMessage)
//  - Message box helper (MsgBox)
//  - Heap hardening (EnableHeapTerminationOnCorruption)
//  - Safer DLL search policy (EnableSafeDllSearch)
//  - Best-effort Per-Monitor DPI awareness (EnableHighDpiAwareness)
//  - Best-effort disable execution-speed power throttling (DisablePowerThrottling)

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "platform/win/LauncherSystemWin.h"

#include <windows.h>
#include <string>

namespace
{
    // Helper: trim trailing whitespace commonly returned by FormatMessageW().
    void TrimRight(std::wstring& s)
    {
        while (!s.empty())
        {
            const wchar_t ch = s.back();
            if (ch == L'\r' || ch == L'\n' || ch == L' ' || ch == L'\t')
                s.pop_back();
            else
                break;
        }
    }

    template <class Fn>
    Fn GetProc(HMODULE mod, const char* name)
    {
        if (!mod)
            return nullptr;
        return reinterpret_cast<Fn>(::GetProcAddress(mod, name));
    }

    // Some constants may be missing depending on Windows SDK version.
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#    define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#    define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#    define LOAD_LIBRARY_SEARCH_USER_DIRS 0x00000400
#endif

#ifndef BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE
#    define BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE 0x00000001
#endif
#ifndef BASE_SEARCH_PATH_PERMANENT
#    define BASE_SEARCH_PATH_PERMANENT 0x00008000
#endif

    // DPI awareness context "magic" values (Windows defines these as special handles).
    // We avoid depending on DPI_AWARENESS_CONTEXT types by treating them as HANDLE.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#    define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)-3)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#    define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

    // shcore.dll PROCESS_DPI_AWARENESS enum (Win 8.1+) — define locally to avoid header deps.
    enum PROCESS_DPI_AWARENESS_LOCAL : int
    {
        PROCESS_DPI_UNAWARE_LOCAL = 0,
        PROCESS_SYSTEM_DPI_AWARE_LOCAL = 1,
        PROCESS_PER_MONITOR_DPI_AWARE_LOCAL = 2
    };

    // Power throttling (SetProcessInformation / ProcessPowerThrottling).
    // PROCESS_POWER_THROTTLING_STATE layout: Version/ControlMask/StateMask are ULONG.
    struct PROCESS_POWER_THROTTLING_STATE_LOCAL
    {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };

    constexpr ULONG PROCESS_POWER_THROTTLING_CURRENT_VERSION_LOCAL = 1;
    constexpr ULONG PROCESS_POWER_THROTTLING_EXECUTION_SPEED_LOCAL = 0x1;

    // PROCESS_INFORMATION_CLASS numeric value for ProcessPowerThrottling.
    // We use the numeric value to avoid SDK/version friction.
    constexpr int PROCESS_INFORMATION_CLASS_ProcessPowerThrottling = 4;

    // Best-effort: compute the current process EXE directory (no filesystem dependency).
    std::wstring GetProcessExeDir()
    {
        std::wstring path;
        path.resize(260); // start with MAX_PATH-ish

        for (;;)
        {
            DWORD len = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (len == 0)
                return L"";

            // If the buffer was too small, GetModuleFileNameW truncates and returns size.
            // We retry with a bigger buffer.
            if (len >= path.size() - 1)
            {
                const size_t newSize = path.size() * 2;
                if (newSize > 32768)
                    break; // absurdly long; fall through and attempt parse best-effort
                path.resize(newSize);
                continue;
            }

            path.resize(len);
            break;
        }

        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";

        path.resize(slash);
        return path;
    }
} // namespace

// -----------------------------------------------------------------------------

std::wstring LastErrorMessage(DWORD err)
{
    if (err == 0)
        return std::wstring();

    LPWSTR buffer = nullptr;

    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;

    const DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

    const DWORD n = ::FormatMessageW(
        flags,
        nullptr,
        err,
        langId,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    std::wstring msg;
    if (n != 0 && buffer)
    {
        msg.assign(buffer, buffer + n);
        TrimRight(msg);
    }
    else
    {
        msg = L"(Unable to format system error message)";
    }

    if (buffer)
        ::LocalFree(buffer);

    return msg;
}

void MsgBox(const std::wstring& title, const std::wstring& text, UINT flags)
{
    // MB_SETFOREGROUND helps ensure the dialog appears on top when launched from Explorer.
    ::MessageBoxW(nullptr, text.c_str(), title.c_str(), flags | MB_SETFOREGROUND);
}

void EnableHeapTerminationOnCorruption()
{
    // Enables terminate-on-corruption for all user-mode heaps in the process.
    (void)::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
}

void EnableSafeDllSearch()
{
    // Goal: reduce "works on dev PC, fails on user PC" dependency resolution issues and
    // avoid unsafe search locations (like the current working directory) where possible.
    //
    // Preferred modern path:
    //   SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    //   AddDllDirectory(<exe-dir>);
    //
    // Fallback:
    //   SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);  (includes application dir)
    // Older fallback:
    //   SetDllDirectoryW(L"");  (removes current directory from search order)

    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");

    using SetDefaultDllDirectoriesFn = BOOL(WINAPI*)(DWORD);
    using DllDirectoryCookie = PVOID;
    using AddDllDirectoryFn = DllDirectoryCookie(WINAPI*)(PCWSTR);

    const auto pSetDefaultDllDirectories =
        GetProc<SetDefaultDllDirectoriesFn>(k32, "SetDefaultDllDirectories");
    const auto pAddDllDirectory =
        GetProc<AddDllDirectoryFn>(k32, "AddDllDirectory");

    if (pSetDefaultDllDirectories)
    {
        if (pAddDllDirectory)
        {
            // Stricter default: do not implicitly search the CWD; do allow "user dirs".
            (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);

            // Ensure we still find DLLs shipped next to the EXE.
            const std::wstring exeDir = GetProcessExeDir();
            if (!exeDir.empty())
            {
                (void)pAddDllDirectory(exeDir.c_str());
            }
            else
            {
                // If we couldn't compute the EXE dir for some reason, fall back to default dirs
                // (which includes the application directory).
                (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            }
        }
        else
        {
            // Can't add directories; use the default safe set (includes application dir).
            (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
    }
    else
    {
        // Legacy fallback: remove current directory from the DLL search path.
        // (Application directory is still searched by default.)
        (void)::SetDllDirectoryW(L"");
    }

    // Optional extra hardening: safe search mode for legacy SearchPath usage.
    using SetSearchPathModeFn = BOOL(WINAPI*)(DWORD);
    const auto pSetSearchPathMode = GetProc<SetSearchPathModeFn>(k32, "SetSearchPathMode");
    if (pSetSearchPathMode)
    {
        (void)pSetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);
    }
}

void EnableHighDpiAwareness()
{
    // Best practice is manifest-based DPI awareness; this is a runtime fallback/override.
    // Prefer PerMonitorV2 if supported, then PerMonitor, then older APIs.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    const auto pSetProcessDpiAwarenessContext =
        GetProc<SetProcessDpiAwarenessContextFn>(user32, "SetProcessDpiAwarenessContext");

    if (pSetProcessDpiAwarenessContext)
    {
        if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;

        if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
            return;

        // If this fails with access denied, the process DPI context is already set
        // (often via manifest). In that case, we should not try to override it.
        if (::GetLastError() == ERROR_ACCESS_DENIED)
            return;
        // Otherwise, fall through to older APIs as a best-effort fallback.
    }

    // Windows 8.1 fallback: shcore!SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
    // (Dynamic-load to avoid hard dependency.)
    if (HMODULE shcore = ::LoadLibraryW(L"shcore.dll"))
    {
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS_LOCAL);
        const auto pSetProcessDpiAwareness =
            GetProc<SetProcessDpiAwarenessFn>(shcore, "SetProcessDpiAwareness");

        if (pSetProcessDpiAwareness)
        {
            (void)pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE_LOCAL);
            ::FreeLibrary(shcore);
            return;
        }

        ::FreeLibrary(shcore);
    }

    // Vista+ fallback: user32!SetProcessDPIAware
    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    const auto pSetProcessDPIAware = GetProc<SetProcessDPIAwareFn>(user32, "SetProcessDPIAware");
    if (pSetProcessDPIAware)
    {
        (void)pSetProcessDPIAware();
    }
}

void DisablePowerThrottling()
{
    // Disables execution-speed power throttling (EcoQoS classification) for the process.
    // This is best-effort; on older Windows versions the API may not exist.
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");

    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, void*, DWORD);
    const auto pSetProcessInformation = GetProc<SetProcessInformationFn>(k32, "SetProcessInformation");
    if (!pSetProcessInformation)
        return;

    PROCESS_POWER_THROTTLING_STATE_LOCAL s{};
    s.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION_LOCAL;
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED_LOCAL; // take control of this policy
    s.StateMask   = 0;                                              // 0 => disable throttling

    (void)pSetProcessInformation(
        ::GetCurrentProcess(),
        PROCESS_INFORMATION_CLASS_ProcessPowerThrottling,
        &s,
        static_cast<DWORD>(sizeof(s))
    );
}
