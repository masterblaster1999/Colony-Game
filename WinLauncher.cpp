// WinLauncher.cpp — Unicode‑safe, single‑instance Windows bootstrapper
// Build: Windows only, C++17 (std::filesystem)
//
// Key behaviors:
//  1) Forces CWD = EXE directory (via winpath::ensure_cwd_exe_dir), so relative assets (res/) load reliably.
//  2) Rebuilds the child's command-line with correct Windows quoting rules.
//  3) Launches the real game EXE via CreateProcessW with lpApplicationName set.
//  4) Enforces single-instance via a named mutex.
//  5) Verifies a sibling res/ directory exists; logs diagnostics to %LOCALAPPDATA%.
//  6) NEW: Enables Per‑Monitor‑V2 DPI awareness (fallback to SetProcessDPIAware) before any UI.
//  7) NEW: Removes current directory from DLL search path; restricts default DLL search dirs.
//  8) NEW: (Optional) Embedded fixed‑timestep game loop wiring. Define COLONY_EMBED_GAME_LOOP
//          to build a single‑binary game that runs in‑process with a stable 60 Hz simulation.
//  9) NEW: Fail‑fast on heap corruption via HeapSetInformation(…, HeapEnableTerminationOnCorruption).
// 10) NEW: Debug console attaches to parent console when present; otherwise allocates a console.
//
// References:
//  - CreateProcessW: https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
//  - GetCommandLineW / CommandLineToArgvW: https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getcommandlinew
//                                           https://learn.microsoft.com/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
//  - SetDefaultDllDirectories / SetDllDirectoryW: https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-setdefaultdlldirectories
//  - DPI awareness APIs: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext
//                        https://learn.microsoft.com/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process
//  - HeapSetInformation (HeapEnableTerminationOnCorruption): https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-heapsetinformation
//  - AttachConsole(ATTACH_PARENT_PROCESS): https://learn.microsoft.com/windows/console/attachconsole

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>       // CommandLineToArgvW
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <cwctype>          // iswspace
#include <ctime>            // localtime_s
#include <cstdio>           // freopen_s

#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

// Fallback typedefs for older SDKs when compiling with dynamic DPI calls only.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#ifndef DPI_AWARENESS_CONTEXT
typedef HANDLE DPI_AWARENESS_CONTEXT;
#endif
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// NEW: centralize Windows path/CWD logic in one place.
#include "platform/win/PathUtilWin.h"

// NEW: Minimal wiring for a fixed‑timestep loop (optional embedded mode)
#include "core/FixedTimestep.h"

namespace fs = std::filesystem;

// ---------- Utilities ----------

static std::wstring LastErrorMessage(DWORD err = GetLastError()) {
    LPWSTR msg = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, nullptr);
    std::wstring out = (len && msg) ? msg : L"";
    if (msg) LocalFree(msg);
    return out;
}

// NEW: Fail‑fast on heap corruption for improved crash diagnosability.
static void EnableHeapTerminationOnCorruption() {
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel) {
        using HeapSetInformation_t = BOOL (WINAPI *)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);
        auto pHeapSetInformation =
            reinterpret_cast<HeapSetInformation_t>(GetProcAddress(hKernel, "HeapSetInformation"));
        if (pHeapSetInformation) {
            pHeapSetInformation(GetProcessHeap(), HeapEnableTerminationOnCorruption, nullptr, 0);
        }
    }
}

// Restrict DLL search order to safe defaults and remove CWD from search path.
// Dynamically resolves SetDefaultDllDirectories for broad OS/SDK compatibility.
static void EnableSafeDllSearch() {
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32) {
        using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
        auto pSetDefaultDllDirectories = reinterpret_cast<PFN_SetDefaultDllDirectories>(
            GetProcAddress(hKernel32, "SetDefaultDllDirectories"));
        if (pSetDefaultDllDirectories) {
            // Only search system locations, the application directory, and explicitly added dirs.
            pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
    }
    // Explicitly remove the current directory from the search path.
    // (Pass L"" to remove CWD; passing NULL would *restore* default search order.)
    SetDllDirectoryW(L"");
}

// NEW: High‑DPI awareness (Per‑Monitor‑V2 if available, else system‑DPI).
static void EnableHighDpiAwareness() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        using PFN_SetProcessDpiAwarenessContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSetCtx = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetCtx) {
            // Best default for desktop games on modern Windows
            if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return;
            }
        }
        using PFN_SetProcessDPIAware = BOOL (WINAPI*)(void);
        auto pSetAware = reinterpret_cast<PFN_SetProcessDPIAware>(
            GetProcAddress(hUser32, "SetProcessDPIAware"));
        if (pSetAware) pSetAware(); // fallback to system DPI awareness
    }
}

// Build a proper Windows command line from argv[1..] using Windows quoting rules so the
// child process parses them the same way CommandLineToArgvW would.
static std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";

    bool needQuotes = false;
    for (wchar_t c : arg) {
        if (iswspace(c) || c == L'"') { needQuotes = true; break; }
    }
    if (!needQuotes) return arg;

    std::wstring result;
    result.push_back(L'"');
    size_t i = 0;
    while (i < arg.size()) {
        // count backslashes
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }

        if (i == arg.size()) {
            // Escape all backslashes at the end (closing quote follows)
            result.append(bs * 2, L'\\');
            break;
        }

        if (arg[i] == L'"') {
            // Escape backslashes and the quote
            result.append(bs * 2 + 1, L'\\');
            result.push_back(L'"');
            ++i;
        } else {
            // Copy backslashes and the next char
            result.append(bs, L'\\');
            result.push_back(arg[i++]);
        }
    }
    result.push_back(L'"');
    return result;
}

static std::wstring BuildCmdLineTail(int argc, wchar_t** argv) {
    std::wstring cmd;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) cmd.push_back(L' ');
        cmd += QuoteArg(argv[i]);
    }
    return cmd;
}

// Simple file logger under %LOCALAPPDATA%\ColonyGame\logs
static fs::path LogsDir() {
    fs::path out = winpath::writable_data_dir() / L"logs";
    std::error_code ec;
    fs::create_directories(out, ec);
    return out;
}

static std::wofstream OpenLogFile() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; localtime_s(&tm, &t);
    std::wstringstream name;
    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";
    std::wofstream f(LogsDir() / name.str(), std::ios::out | std::ios::trunc);
    return f;
}

static void MsgBox(const std::wstring& title, const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static bool VerifyResources() {
    std::error_code ec;
    const auto res = winpath::resource_dir();
    return fs::exists(res, ec) && fs::is_directory(res, ec);
}

// Find the child game EXE next to the launcher.
// Optional override via res/launcher.cfg: first non-empty line is the EXE filename.
static fs::path ResolveGameExe(const fs::path& baseDir) {
    std::error_code ec;
    auto cfg = winpath::resource_dir() / L"launcher.cfg";
    if (fs::exists(cfg, ec)) {
        std::wifstream fin(cfg);
        std::wstring name;
        if (fin && std::getline(fin, name) && !name.empty()) {
            // trim CR/LF
            while (!name.empty() && (name.back() == L'\r' || name.back() == L'\n')) name.pop_back();
            fs::path cand = baseDir / name;
            if (fs::exists(cand, ec)) return cand;
        }
    }
    // Fall back to common names
    const wchar_t* candidates[] = { L"ColonyGame.exe", L"Colony-Game.exe", L"Game.exe" };
    for (auto* n : candidates) {
        fs::path p = baseDir / n;
        if (fs::exists(p, ec)) return p;
    }
    return {};
}

// Single-instance guard using a named mutex
class SingleInstanceGuard {
    HANDLE h_ = nullptr;
public:
    bool acquire(const std::wstring& name) {
        h_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!h_) return false;
        return GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~SingleInstanceGuard() { if (h_) CloseHandle(h_); }
};

#if defined(COLONY_EMBED_GAME_LOOP)
// ---------------------- Optional embedded game loop -------------------------
static bool g_pauseRequested = false;
static bool g_stepRequested  = false;
static HWND g_mainWnd = nullptr;

static LRESULT CALLBACK EmbeddedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == 'P') { g_pauseRequested = !g_pauseRequested; return 0; }
        if (wParam == 'O') { g_stepRequested = true; return 0; }
        if (wParam == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Stub hooks you can wire to your real game systems.
static void Game_Update(double /*dt*/) {
    // TODO: call into your engine update (AI/physics/gameplay).
}
static void Game_Render(double /*alpha*/) {
    // TODO: call into your renderer; Present() etc.
}

static int RunEmbeddedGameLoop(std::wofstream& logFile) {
    // Register a minimal window class
    WNDCLASSW wc{};
    wc.lpfnWndProc   = EmbeddedWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ColonyGameEmbeddedWndClass";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassW(&wc)) {
        MsgBox(L"Colony Game", L"Failed to register window class.");
        return 3;
    }

    // Create the main window
    g_mainWnd = CreateWindowExW(
        0, wc.lpszClassName, L"Colony Game (embedded)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_mainWnd) {
        MsgBox(L"Colony Game", L"Failed to create window.");
        return 3;
    }

    ShowWindow(g_mainWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_mainWnd);

    core::FixedTimestep loop(60.0);     // 60 Hz simulation
    loop.set_max_steps_per_frame(180);   // safety cap

    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        loop.set_paused(g_pauseRequested);
        if (g_stepRequested) { loop.step_once(); g_stepRequested = false; }

        auto stats = loop.tick(
            [&](double dt)    { Game_Update(dt);    },
            [&](double alpha) { Game_Render(alpha); }
        );

        // Optional: emit quick perf line to the log
        if ((stats.total_steps % 120) == 0) {
            logFile << L"[Loop] fps=" << stats.fps
                    << L" steps=" << stats.steps_this_frame
                    << L" alpha=" << stats.alpha << L"\n";
            logFile.flush();
        }
    }

    DestroyWindow(g_mainWnd);
    g_mainWnd = nullptr;
    return 0;
}
#endif // COLONY_EMBED_GAME_LOOP

#ifdef _DEBUG
// NEW: Prefer attaching to an existing parent console (if launched from a terminal).
static void AttachParentConsoleOrAlloc() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
}
#endif

// ---------- Entry point ----------
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // NEW: Enable fail-fast behavior on heap corruption as early as possible.
    EnableHeapTerminationOnCorruption();

    // Must run before any library loads to constrain DLL search order.
    EnableSafeDllSearch();

    // Ensure asset-relative paths work from any launch context (Explorer, VS, cmd).
    winpath::ensure_cwd_exe_dir();

    // Set error mode early to avoid OS popups for missing DLLs, etc.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    // NEW: Make message boxes crisp under high DPI scaling.
    EnableHighDpiAwareness();

#ifdef _DEBUG
    // NEW: Attach to parent console when present; otherwise allocate a console.
    AttachParentConsoleOrAlloc();
#endif

    SingleInstanceGuard guard;
    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF")) {
        MsgBox(L"Colony Game", L"Another instance is already running.");
        return 0;
    }

    auto exeDir = winpath::exe_dir();
    auto log = OpenLogFile();
    log << L"[Launcher] started in: " << exeDir.wstring() << L"\n";

#if defined(COLONY_EMBED_GAME_LOOP)
    // Optional single-binary mode: run the game loop in-process instead of spawning a child.
    log << L"[Launcher] COLONY_EMBED_GAME_LOOP defined; running embedded loop.\n";
    if (!VerifyResources()) {
        MsgBox(L"Colony Game",
               L"Missing or invalid 'res' folder next to the executable.\n"
               L"Make sure the game is installed correctly.");
        log << L"[Launcher] res/ check failed\n";
        return 1;
    }
    return RunEmbeddedGameLoop(log);
#else
    if (!VerifyResources()) {
        MsgBox(L"Colony Game",
               L"Missing or invalid 'res' folder next to the executable.\n"
               L"Make sure the game is installed correctly.");
        log << L"[Launcher] res/ check failed\n";
        return 1;
    }

    // Resolve child game EXE
    auto gameExe = ResolveGameExe(exeDir);
    if (gameExe.empty()) {
        MsgBox(L"Colony Game",
               L"Could not find the game executable next to the launcher.\n"
               L"Looked for 'ColonyGame.exe', 'Colony-Game.exe', or 'Game.exe'.\n"
               L"You can override via 'res/launcher.cfg'.");
        log << L"[Launcher] no child EXE found\n";
        return 1;
    }

    // Build arguments *without* embedding exe path (pass exe via lpApplicationName).
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); // see docs
    std::wstring tail = BuildCmdLineTail(argc, argv);
    if (argv) LocalFree(argv);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdline = tail; // mutable buffer for CreateProcessW

    log << L"[Launcher] launching: " << gameExe.wstring() << L" args: " << tail << L"\n";

    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT
                        | CREATE_DEFAULT_ERROR_MODE; // child does not inherit our error mode

    BOOL ok = CreateProcessW(
        gameExe.wstring().c_str(),                    // lpApplicationName (no quotes here)
        cmdline.empty() ? nullptr : cmdline.data(),   // lpCommandLine (only args, properly quoted)
        nullptr, nullptr, FALSE,
        creationFlags,
        nullptr,
        exeDir.wstring().c_str(),                     // ensure correct CWD for assets
        &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        std::wstring msg = L"Failed to start game process.\n\nError "
                         + std::to_wstring(err) + L": " + LastErrorMessage(err);
        MsgBox(L"Colony Game", msg);
        log << L"[Launcher] CreateProcessW failed: " << err
            << L" : " << LastErrorMessage(err) << L"\n";
        return 2;
    }

    // Clean up; let the game own the lifetime
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log << L"[Launcher] success; exiting.\n";
    return 0;
#endif
}
