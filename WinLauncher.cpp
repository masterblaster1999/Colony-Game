// WinLauncher.cpp (Windows GUI subsystem launcher)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>   // PathRemoveFileSpecW
#include <string>
#include <vector>
#include "src/platform/win/SingleInstanceGuard.h"
#include "src/platform/win/CrashHandler.h"

#pragma comment(lib, "Shlwapi.lib")

// A unique name for the single-instance mutex and activation message.
static const wchar_t* kMutexName   = L"Global\\ColonyGame_SingleInstance_{B93D3CFF-0A14-48A2-8D40-3D86B479D637}";
static const wchar_t* kActivateMsg = L"COLONY_GAME_ACTIVATE_{0B9E6E3A-B2BA-4E95-96C4-7CF9E8AF8F5E}";
static UINT g_WM_ACTIVATE = 0;

// Fallback defines to avoid SDK version friction.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)-3)
#endif

static void EnableDpiAwareness() {
    // Prefer Per-Monitor V2 if available, then Per-Monitor, then system DPI-aware.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");

    if (user32) {
        using SetProcessDpiAwarenessContextFn = BOOL (WINAPI*)(HANDLE);
        auto fpSetProcessDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fpSetProcessDpiAwarenessContext) {
            if (fpSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
            if (fpSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))   return;
        }
    }

    HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        using SetProcessDpiAwarenessFn = HRESULT (WINAPI*)(int); // PROCESS_DPI_AWARENESS
        auto fpSetProcessDpiAwareness =
            reinterpret_cast<SetProcessDpiAwarenessFn>(
                ::GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fpSetProcessDpiAwareness) {
            // 2 == PROCESS_PER_MONITOR_DPI_AWARE
            fpSetProcessDpiAwareness(2);
            ::FreeLibrary(shcore);
            return;
        }
        ::FreeLibrary(shcore);
    }

    if (user32) {
        using SetProcessDPIAwareFn = BOOL (WINAPI*)();
        auto fpSetProcessDPIAware =
            reinterpret_cast<SetProcessDPIAwareFn>(
                ::GetProcAddress(user32, "SetProcessDPIAware"));
        if (fpSetProcessDPIAware) {
            fpSetProcessDPIAware();
        }
    }
}

static void ConfigureErrorModes() {
    // Reduce OS error UI so crashes go straight to our crash handler + exit.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

static void SetWorkingDirToExe() {
    wchar_t exePath[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    ::PathRemoveFileSpecW(exePath);     // shlwapi: strips filename -> folder
    ::SetCurrentDirectoryW(exePath);
}

static std::wstring BuildChildCmdLine(const wchar_t* exeName) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    std::wstring cl = L"\"";
    cl += exeName;
    cl += L"\"";

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            cl += L" \"";
            cl += argv[i];
            cl += L"\"";
        }
        ::LocalFree(argv);
    }
    return cl;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // (0) high-DPI correctness + cleaner crash behavior
    EnableDpiAwareness();
    ConfigureErrorModes();

    SetWorkingDirToExe();                             // (1) stable CWD for relative assets

    // Ensure dumps folder exists even if CrashHandler doesn't create it.
    ::CreateDirectoryW(L"dumps", nullptr);

    InitCrashHandler(L"dumps");                       // (2) crash dumps on failure
    g_WM_ACTIVATE = ::RegisterWindowMessageW(kActivateMsg);

    // (3) single-instance guard: show/focus the running one if present
    SingleInstanceGuard guard(kMutexName);
    if (!guard.IsPrimary()) {
        ::PostMessageW(HWND_BROADCAST, g_WM_ACTIVATE, 0, 0);
        return 0;
    }

    // (4) helpful check if assets folder is missing (must be a directory)
    DWORD attrs = ::GetFileAttributesW(L"res");
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        ::MessageBoxW(nullptr,
            L"'res' folder not found next to the executable.\n"
            L"Make sure the game is unpacked correctly.",
            L"Colony Game", MB_ICONERROR | MB_OK);
        return 2;
    }

    // (5) launch the actual game exe that lives next to the launcher
    const wchar_t* gameExe = L"ColonyGame.exe"; // <— update if your binary name differs
    std::wstring cmdLine   = BuildChildCmdLine(gameExe);

    // CreateProcessW requires a writable buffer for the command line on some toolsets.
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::wstring msg = L"Failed to launch '";
        msg += gameExe;
        msg += L"' (error ";
        msg += std::to_wstring(::GetLastError());
        msg += L").";
        ::MessageBoxW(nullptr, msg.c_str(), L"Colony Game", MB_ICONERROR | MB_OK);
        return 3;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return 0;
}
