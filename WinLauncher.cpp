// WinLauncher.cpp
//
// Windows-only launcher for Colony Game.
//
// Responsibilities:
//  - Process-wide Win32 setup (DLL search, DPI, power hints)
//  - Crash handler bootstrap
//  - Single-instance guard
//  - Friendly preflight checks for content/shader folders
//  - Locating and spawning the main game executable
//  - Optional embedded "safe mode" game loop (COLONY_EMBED_GAME_LOOP)
//
// All low-level helpers live in platform/win/Launcher*.h/.cpp.

#ifndef UNICODE
#   define UNICODE
#endif

#ifndef _UNICODE
#   define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <windows.h>
#include <shobjidl.h>    // SetCurrentProcessExplicitAppUserModelID
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _DEBUG
#   include <cstdio>     // freopen_s for debug console (may be used by LauncherSystemWin.cpp)
#endif

#include "platform/win/PathUtilWin.h"
#include "platform/win/CrashHandlerWin.h"
#include "platform/win/LauncherSystemWin.h"
#include "platform/win/LauncherLoggingWin.h"
#include "platform/win/LauncherCliWin.h"
#include "platform/win/LauncherInstanceWin.h"

// New split modules
#include "platform/win/LauncherPreflightWin.h"
#include "platform/win/LauncherSpawnWin.h"
#include "platform/win/LauncherEmbeddedSafeModeWin.h"

namespace fs = std::filesystem;

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // 1) Crash handler: set up as early as possible.
    wincrash::InitCrashHandler(L"Colony Game");

    // 2) Parse simple CLI toggles and overrides.
    const bool skipPreflight = HasFlag(L"skip-preflight");
    const bool noSingleton   = HasFlag(L"no-singleton");

    std::wstring exeOverride;
    (void)TryGetArgValue(L"exe", exeOverride); // --exe=Foo.exe or --exe Foo.exe

    // 3) Process-wide Win32 setup.
    EnableHeapTerminationOnCorruption();
    EnableSafeDllSearch();

    // Ensure asset-relative paths work no matter how we were launched.
    winpath::ensure_cwd_exe_dir();

    // Suppress OS error UI for missing DLLs, etc.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    // Make message boxes crisp under high DPI; hint to avoid laptop throttling.
    EnableHighDpiAwareness();
    DisablePowerThrottling();

#ifdef _DEBUG
    // Attach to parent console if we were launched from a terminal.
    AttachParentConsoleOrAlloc();
#endif

    // Better taskbar grouping / identity.
    SetCurrentProcessExplicitAppUserModelID(L"ColonyGame.Colony");

    // 4) Logging.
    std::wofstream log = OpenLogFile();
    WriteLog(log, L"[Launcher] Colony Game Windows launcher starting.");
    WriteLog(log, L"[Launcher] EXE dir   : " + winpath::exe_dir().wstring());
    WriteLog(log, L"[Launcher] CWD       : " + fs::current_path().wstring());
    WriteLog(log, L"[Launcher] User data : " + winpath::writable_data_dir().wstring());

    // 5) Single-instance guard (optional).
    SingleInstanceGuard guard;
    if (!noSingleton)
    {
        if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF"))
        {
            MsgBox(L"Colony Game", L"Another instance is already running.");
            return 0;
        }
    }

#ifdef COLONY_EMBED_GAME_LOOP
    // Optional "safe mode": force embedded loop with --safe or /safe.
    if (HasFlag(L"safe"))
    {
        WriteLog(log, L"[Launcher] --safe specified: running embedded safe mode.");
        return winlaunch::RunEmbeddedGameLoop(log);
    }
#endif

    // 6) Preflight checks for content + shader folders.
    if (!skipPreflight)
    {
        std::wstring preflightError;
        if (!winlaunch::CheckEssentialFiles(fs::current_path(), preflightError, log))
        {
            WriteLog(log, L"[Launcher] Preflight checks failed.");
            MsgBox(L"Colony Game - Startup Error", preflightError);
            return 2;
        }
    }
    else
    {
        WriteLog(log, L"[Launcher] Preflight checks skipped via --skip-preflight.");
    }

    // 7) Locate the game executable.
    const fs::path exeDir = fs::current_path();

    std::vector<fs::path> tried;
    const fs::path gameExe = winlaunch::FindGameExecutable(exeDir, exeOverride, log, &tried);

    if (gameExe.empty())
    {
        const std::wstring msg = winlaunch::BuildExeNotFoundMessage(tried);

#ifdef COLONY_EMBED_GAME_LOOP
        WriteLog(log, L"[Launcher] EXE missing; falling back to embedded safe mode.");
        MsgBox(L"Colony Game - Safe Mode",
               L"Game EXE not found. Launching embedded safe mode.");
        return winlaunch::RunEmbeddedGameLoop(log);
#else
        MsgBox(L"Colony Game - Startup Error", msg);
        WriteLog(log, L"[Launcher] " + msg);
        return 3;
#endif
    }

    // 8) Build the child command line (skips argv[0]).
    const std::wstring childArgs = BuildChildArguments();

    // 9) Spawn + wait + mirror exit code.
    const winlaunch::SpawnResult spawn = winlaunch::SpawnAndWait(gameExe, exeDir, childArgs, log);

    if (!spawn.succeeded)
    {
        WriteLog(
            log,
            L"[Launcher] CreateProcessW failed (" +
            std::to_wstring(spawn.win32_error) + L"): " + spawn.win32_error_text
        );

#ifdef COLONY_EMBED_GAME_LOOP
        WriteLog(log, L"[Launcher] Falling back to embedded safe mode.");
        MsgBox(
            L"Colony Game - Safe Mode",
            L"Failed to start the main game process.\n"
            L"Launching embedded safe mode instead."
        );
        return winlaunch::RunEmbeddedGameLoop(log);
#else
        std::wstring msg =
            L"Failed to start game process.\n\n"
            L"Error " + std::to_wstring(spawn.win32_error) + L": " + spawn.win32_error_text;

        MsgBox(L"Colony Game", msg);
        return 3;
#endif
    }

    WriteLog(log, L"[Launcher] Game exited with code " + std::to_wstring(spawn.exit_code));
    return static_cast<int>(spawn.exit_code);
}
