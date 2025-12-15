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
#include <optional>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#ifdef _DEBUG
#   include <cstdio>     // freopen_s for debug console
#endif

#include "platform/win/PathUtilWin.h"
#include "platform/win/CrashHandlerWin.h"
#include "platform/win/LauncherSystemWin.h"
#include "platform/win/LauncherLoggingWin.h"
#include "platform/win/LauncherCliWin.h"
#include "platform/win/LauncherInstanceWin.h"
#include "platform/win/DpiMessagesWin.h" // <-- PATCH: WM_DPICHANGED handling helpers

#ifdef COLONY_EMBED_GAME_LOOP
#   include "colony/world/World.h"
#   include "colony/loop/GameLoop.h"
#endif

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Hybrid GPU hints (NVidia / AMD).
// NOTE: The exported globals are defined once in platform/win/HighPerfGPU.cpp
// to avoid duplicate-symbol warnings.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Local helpers that are still logically "launcher orchestration"
// (preflight + exe name override + optional embedded safe mode).
// -----------------------------------------------------------------------------

// Check that required content/shader folders exist under `root`.
// Writes per-folder info into the log stream.
static bool CheckEssentialFiles(const fs::path&   root,
                                std::wstring&     errorOut,
                                std::wofstream&   log)
{
    struct Group
    {
        std::vector<fs::path> anyOf;
        const wchar_t*        label;
    };

    // At least one path in each group must exist.
    std::vector<Group> groups = {
        // Content roots (allow "resources" as well as "assets" / "res").
        {
            { root / L"assets", root / L"res", root / L"resources" },
            L"Content (assets, res, or resources)"
        },
        // Shader roots (either legacy or new location).
        {
            { root / L"renderer" / L"Shaders", root / L"shaders" },
            L"Shaders (renderer/Shaders or shaders)"
        }
    };

    std::wstringstream missing;
    bool ok = true;

    for (const auto& g : groups)
    {
        bool found = false;

        for (const auto& p : g.anyOf)
        {
            if (fs::exists(p))
            {
                WriteLog(log, L"[Launcher] Found: " + p.wstring());
                found = true;
                break;
            }
        }

        if (!found)
        {
            ok = false;
            missing << L" - " << g.label << L"\n";
        }
    }

    if (!ok)
    {
        errorOut =
            L"Missing required content folders:\n\n" +
            missing.str() +
            L"\nPlease verify your installation directory contains the folders above.";
    }

    return ok;
}

// Optional environment override for the game EXE path.
//   COLONY_GAME_EXE="C:\foo\bar\MyGame.exe"  (absolute)
//   COLONY_GAME_EXE="ColonyGame.exe"         (relative to launcher dir)
static std::optional<fs::path> EnvExeOverride()
{
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(L"COLONY_GAME_EXE", buf, 1024);
    if (n != 0 && n < 1024)
        return fs::path(buf);

    return std::nullopt;
}

#ifdef COLONY_EMBED_GAME_LOOP

// Forward declarations for the embedded safe-mode loop.
static LRESULT CALLBACK EmbeddedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static int RunEmbeddedGameLoop(std::wostream& log);

#endif // COLONY_EMBED_GAME_LOOP

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

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
        return RunEmbeddedGameLoop(log);
    }
#endif

    // 6) Preflight checks for content + shader folders.
    if (!skipPreflight)
    {
        std::wstring preflightError;
        if (!CheckEssentialFiles(fs::current_path(), preflightError, log))
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

    std::vector<fs::path> candidates;

    // CLI override has highest priority.
    if (!exeOverride.empty())
        candidates.push_back(exeDir / exeOverride);

    // Environment override is next.
    if (auto envExe = EnvExeOverride())
    {
        if (envExe->is_absolute())
            candidates.push_back(*envExe);
        else
            candidates.push_back(exeDir / *envExe);
    }

    // Common target names (both old and new), plus a bin/ variant.
    candidates.push_back(exeDir / L"ColonyGame.exe");
    candidates.push_back(exeDir / L"Colony-Game.exe");
    candidates.push_back(exeDir / L"Colony.exe");
    candidates.push_back(exeDir / L"bin" / L"ColonyGame.exe");

    fs::path gameExe;
    for (const auto& c : candidates)
    {
        if (fs::exists(c))
        {
            gameExe = c;
            break;
        }
    }

    if (gameExe.empty())
    {
        std::wstring msg = L"Could not find the game executable.\nTried:\n";
        for (const auto& c : candidates)
        {
            msg += L" - " + c.wstring() + L"\n";
        }

#ifdef COLONY_EMBED_GAME_LOOP
        WriteLog(log, L"[Launcher] EXE missing; falling back to embedded safe mode.");
        MsgBox(L"Colony Game - Safe Mode",
               L"Game EXE not found. Launching embedded safe mode.");
        return RunEmbeddedGameLoop(log);
#else
        MsgBox(L"Colony Game - Startup Error", msg);
        WriteLog(log, L"[Launcher] " + msg);
        return 3;
#endif
    }

    // 8) Build the child command line.
    std::wstring args = BuildChildArguments();

    // Include the quoted EXE as argv[0] in the child command line.
    std::wstring cmd = QuoteArgWindows(gameExe.wstring());
    if (!args.empty())
    {
        cmd.push_back(L' ');
        cmd.append(args);
    }

    STARTUPINFOW        si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE;

    // Create mutable command-line buffer (Windows API requirement).
    std::vector<wchar_t> cmdMutable(cmd.begin(), cmd.end());
    cmdMutable.push_back(L'\0');

    WriteLog(
        log,
        L"[Launcher] Spawning: " + gameExe.wstring() +
        (args.empty() ? L"" : L" " + args)
    );

    BOOL ok = CreateProcessW(
        gameExe.c_str(),       // lpApplicationName
        cmdMutable.data(),     // lpCommandLine (mutable buffer)
        nullptr,               // lpProcessAttributes
        nullptr,               // lpThreadAttributes
        FALSE,                 // bInheritHandles
        creationFlags,         // dwCreationFlags
        nullptr,               // lpEnvironment (inherit ours)
        exeDir.c_str(),        // lpCurrentDirectory
        &si,
        &pi
    );

    if (!ok)
    {
        DWORD err = GetLastError();
        const std::wstring errText = LastErrorMessage(err);

        WriteLog(
            log,
            L"[Launcher] CreateProcessW failed (" +
            std::to_wstring(err) + L"): " + errText
        );

#ifdef COLONY_EMBED_GAME_LOOP
        WriteLog(log, L"[Launcher] Falling back to embedded safe mode.");
        MsgBox(
            L"Colony Game - Safe Mode",
            L"Failed to start the main game process.\n"
            L"Launching embedded safe mode instead."
        );
        return RunEmbeddedGameLoop(log);
#else
        std::wstring msg =
            L"Failed to start game process.\n\n"
            L"Error " + std::to_wstring(err) + L": " + errText;

        MsgBox(L"Colony Game", msg);
        return 3;
#endif
    }

    // 9) Wait for the game process to exit, then mirror its exit code.
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    WriteLog(log, L"[Launcher] Game exited with code " + std::to_wstring(code));

    return static_cast<int>(code);
}

// -----------------------------------------------------------------------------
// Embedded "safe mode" loop (optional)
// -----------------------------------------------------------------------------
#ifdef COLONY_EMBED_GAME_LOOP

namespace
{
    struct EmbeddedState
    {
        colony::RenderSnapshot snapshot;
    };

    EmbeddedState   g_state;
    windpi::DpiState g_embedded_dpi; // <-- PATCH: per-window DPI state for the embedded GDI view
}

static LRESULT CALLBACK EmbeddedWndProc(HWND hwnd,
                                        UINT  msg,
                                        WPARAM wParam,
                                        LPARAM lParam)
{
    // --- PATCH: handle per-monitor DPI changes (WM_DPICHANGED) ---
    // This keeps the window's physical size consistent when moved between monitors
    // with different scaling, and gives us a live DPI scale for drawing.
    LRESULT dpiResult = 0;
    if (windpi::TryHandleMessage(hwnd, msg, wParam, lParam, g_embedded_dpi, dpiResult))
    {
        InvalidateRect(hwnd, nullptr, FALSE);
        return dpiResult;
    }

    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC         dc = BeginPaint(hwnd, &ps);

            RECT rc{};
            GetClientRect(hwnd, &rc);

            // Background
            HBRUSH bg = CreateSolidBrush(RGB(32, 32, 48));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(220, 220, 230));

            HFONT font    = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));

            const int   w     = rc.right - rc.left;
            const int   h     = rc.bottom - rc.top;
            const float scale = 60.0f * g_embedded_dpi.scale; // <-- PATCH: DPI-aware scale
            const float cx    = w * 0.5f;
            const float cy    = h * 0.5f;

            // Agents
            HBRUSH agentBrush = CreateSolidBrush(RGB(80, 200, 255));
            HBRUSH oldBrush   = static_cast<HBRUSH>(SelectObject(dc, agentBrush));

            HPEN pen    = CreatePen(PS_SOLID, 1, RGB(20, 120, 180));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

            for (const auto& p : g_state.snapshot.agent_positions)
            {
                const int x = static_cast<int>(cx + static_cast<float>(p.x) * scale);
                const int y = static_cast<int>(cy - static_cast<float>(p.y) * scale);
                const int r = static_cast<int>(6.0f * g_embedded_dpi.scale); // <-- PATCH: DPI-aware radius

                Ellipse(dc, x - r, y - r, x + r, y + r);
            }

            SelectObject(dc, oldPen);
            DeleteObject(pen);

            SelectObject(dc, oldBrush);
            DeleteObject(agentBrush);

            // HUD
            std::wstringstream hud;
            hud << L"Embedded Safe Mode | sim_step=" << g_state.snapshot.sim_step
                << L"  sim_time=" << std::fixed << std::setprecision(2)
                << g_state.snapshot.sim_time;

            const std::wstring hudText = hud.str();

            // <-- PATCH: keep HUD padding roughly constant in physical size
            const int pad = windpi::DipToPx(8, g_embedded_dpi.dpi);

            TextOutW(dc, pad, pad, hudText.c_str(),
                     static_cast<int>(hudText.size()));

            SelectObject(dc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static int RunEmbeddedGameLoop(std::wostream& log)
{
    // 1) Simple Win32 window (no D3D, just GDI).
    HINSTANCE      hInst  = GetModuleHandleW(nullptr);
    const wchar_t* kClass = L"ColonyEmbeddedGameWindow";

    WNDCLASSW wc{};
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &EmbeddedWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;

    if (!RegisterClassW(&wc))
    {
        MsgBox(L"Colony Game", L"Failed to register embedded window class.");
        return 10;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kClass,
        L"Colony Game (Embedded Safe Mode)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hwnd)
    {
        MsgBox(L"Colony Game", L"Failed to create embedded window.");
        return 11;
    }

    // --- PATCH: initialize DPI state immediately so drawing scale is correct from frame 1 ---
    windpi::InitFromHwnd(hwnd, g_embedded_dpi);

    // 2) Build the world and run a fixed-timestep loop.
    colony::World          world;
    colony::GameLoopConfig cfg{};
    cfg.fixed_dt             = 1.0 / 60.0;
    cfg.max_frame_time       = 0.25;
    cfg.max_updates_per_frame = 5;
    cfg.run_when_minimized   = false;

    auto render = [&](const colony::World& w, float alpha)
    {
        g_state.snapshot = w.snapshot(alpha);
        InvalidateRect(hwnd, nullptr, FALSE);
    };

    WriteLog(log, L"[Embedded] Running fixed-timestep loop.");

    const int exitCode = colony::RunGameLoop(world, render, hwnd, cfg);

    DestroyWindow(hwnd);
    UnregisterClassW(kClass, hInst);

    return exitCode;
}

#endif // COLONY_EMBED_GAME_LOOP
