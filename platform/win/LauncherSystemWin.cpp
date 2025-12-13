// platform/win/LauncherSystemWin.cpp
//
// Windows-only process/system helpers used by WinLauncher.cpp.
// This file replaces the previous placeholder and provides the real
// implementations declared in platform/win/LauncherSystemWin.h.

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
    // From PROCESS_POWER_THROTTLING_STATE docs: Version/ControlMask/StateMask are ULONG. :contentReference[oaicite:0]{index=0}
    struct PROCESS_POWER_THROTTLING_STATE_LOCAL
    {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };

    constexpr ULONG PROCESS_POWER_THROTTLING_CURRENT_VERSION_LOCAL = 1;
    constexpr ULONG PROCESS_POWER_THROTTLING_EXECUTION_SPEED_LOCAL = 0x1;

    // PROCESS_INFORMATION_CLASS value for ProcessPowerThrottling:
    // The enum order in MS docs shows it after ProcessInPrivateInfo. :contentReference[oaicite:1]{index=1}
    // In practice this is 4 on modern SDKs (0-based). We use the numeric value to avoid SDK friction.
    constexpr int PROCESS_INFORMATION_CLASS_ProcessPowerThrottling = 4;
} // namespace

// -----------------------------------------------------------------------------

std::wstring LastErrorMessage(DWORD err)
{
    if (err == 0)
        return std::wstring();

    LPWSTR  buffer = nullptr;
    DWORD   flags  = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD   langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

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
    // Enables terminate-on-corruption for all user-mode heaps in the process. :contentReference[oaicite:2]{index=2}
    ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
}

void EnableSafeDllSearch()
{
    // Prefer SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) when available. :contentReference[oaicite:3]{index=3}
    // This removes the current directory from the default DLL search order and limits search to safer dirs.
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");

    using SetDefaultDllDirectoriesFn = BOOL(WINAPI*)(DWORD);
    auto pSetDefaultDllDirectories =
        GetProc<SetDefaultDllDirectoriesFn>(k32, "SetDefaultDllDirectories");

    if (pSetDefaultDllDirectories)
    {
        (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    else
    {
        // Fallback: remove current directory from the search path.
        // (Application directory is still searched by default.)
        (void)::SetDllDirectoryW(L"");
    }

    // Optional extra hardening: safe search mode for legacy SearchPath usage.
    using SetSearchPathModeFn = BOOL(WINAPI*)(DWORD);
    auto pSetSearchPathMode = GetProc<SetSearchPathModeFn>(k32, "SetSearchPathMode");
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
    auto pSetProcessDpiAwarenessContext =
        GetProc<SetProcessDpiAwarenessContextFn>(user32, "SetProcessDpiAwarenessContext");

    if (pSetProcessDpiAwarenessContext)
    {
        // Try Per Monitor v2 first, then Per Monitor.
        if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;

        (void)pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        return;
    }

    // Windows 8.1 fallback: shcore!SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
    // (Dynamic-load to avoid hard dependency.)
    if (HMODULE shcore = ::LoadLibraryW(L"shcore.dll"))
    {
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS_LOCAL);
        auto pSetProcessDpiAwareness =
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
    auto pSetProcessDPIAware = GetProc<SetProcessDPIAwareFn>(user32, "SetProcessDPIAware");
    if (pSetProcessDPIAware)
    {
        (void)pSetProcessDPIAware();
    }
}

void DisablePowerThrottling()
{
    // Use SetProcessInformation(..., ProcessPowerThrottling, ...) where available. :contentReference[oaicite:4]{index=4}
    // Disables execution-speed power throttling (EcoQoS classification) for the process.
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");

    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, void*, DWORD);
    auto pSetProcessInformation = GetProc<SetProcessInformationFn>(k32, "SetProcessInformation");
    if (!pSetProcessInformation)
        return;

    PROCESS_POWER_THROTTLING_STATE_LOCAL s{};
    s.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION_LOCAL;
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED_LOCAL; // we take control of this policy
    s.StateMask   = 0;                                              // 0 => disable throttling

    (void)pSetProcessInformation(
        ::GetCurrentProcess(),
        PROCESS_INFORMATION_CLASS_ProcessPowerThrottling,
        &s,
        static_cast<DWORD>(sizeof(s))
    );
}
