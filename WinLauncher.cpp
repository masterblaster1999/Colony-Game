// WinLauncher.cpp (Windows GUI subsystem launcher)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>   // PathRemoveFileSpecW
#include <string>
#include "src/platform/win/SingleInstanceGuard.h"
#include "src/platform/win/CrashHandler.h"

#pragma comment(lib, "Shlwapi.lib")

// A unique name for the single-instance mutex and activation message.
static const wchar_t* kMutexName   = L"Global\\ColonyGame_SingleInstance_{B93D3CFF-0A14-48A2-8D40-3D86B479D637}";
static const wchar_t* kActivateMsg = L"COLONY_GAME_ACTIVATE_{0B9E6E3A-B2BA-4E95-96C4-7CF9E8AF8F5E}";
static UINT g_WM_ACTIVATE = 0;

static void SetWorkingDirToExe() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);     // shlwapi: strips filename -> folder
    SetCurrentDirectoryW(exePath);
}

static std::wstring BuildChildCmdLine(const wchar_t* exeName) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cl = L"\""; cl += exeName; cl += L"\"";
    for (int i = 1; i < argc; ++i) { cl += L" \""; cl += argv[i]; cl += L"\""; }
    LocalFree(argv);
    return cl;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    SetWorkingDirToExe();                             // (1) stable CWD for relative assets
    InitCrashHandler(L"dumps");                       // (2) crash dumps on failure
    g_WM_ACTIVATE = RegisterWindowMessageW(kActivateMsg);

    // (3) single-instance guard: show/focus the running one if present
    SingleInstanceGuard guard(kMutexName);
    if (!guard.IsPrimary()) {
        PostMessageW(HWND_BROADCAST, g_WM_ACTIVATE, 0, 0);
        return 0;
    }

    // (4) helpful check if assets folder is missing
    if (GetFileAttributesW(L"res") == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr,
            L"'res' folder not found next to the executable.\n"
            L"Make sure the game is unpacked correctly.",
            L"Colony Game", MB_ICONERROR | MB_OK);
        return 2;
    }

    // (5) launch the actual game exe that lives next to the launcher
    const wchar_t* gameExe = L"ColonyGame.exe"; // <— update if your binary name differs
    std::wstring cmdLine   = BuildChildCmdLine(gameExe);

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::wstring msg = L"Failed to launch '";
        msg += gameExe;
        msg += L"' (error " + std::to_wstring(GetLastError()) + L").";
        MessageBoxW(nullptr, msg.c_str(), L"Colony Game", MB_ICONERROR | MB_OK);
        return 3;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

