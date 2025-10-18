// / WinLauncher.cpp — Windows-only launcher hardened for startup reliability.
//
// Patches applied:
//  0.1 Working directory -> EXE folder
//  0.2 DPI awareness + single-instance + discrete-GPU hint
//  0.3 Friendly preflight checks for res/, assets/, shaders/
//  0.4 Crash dumps on unhandled exceptions (via wincrash::InitCrashHandler)
//  0.5 D3D12 Agility SDK lookup via AddDllDirectory(".\\D3D12") with safe DLL search
//
// Plus: Safe DLL search order, UTF‑16 log files (with BOM), and clear logging.
// Embedded safe‑mode game loop: define COLONY_EMBED_GAME_LOOP to enable.
// Build: MSVC /std:c++20 (or CMake CXX_STANDARD 20)
//

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <shlobj.h>     // SHGetKnownFolderPath (used by path utils / logs)
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cwctype>      // iswspace
#include <ctime>        // localtime_s
#include <cstdio>       // freopen_s
#include <system_error>

#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#  define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#  define LOAD_LIBRARY_SEARCH_USER_DIRS 0x00000400
#endif
#ifndef DLL_DIRECTORY_COOKIE
typedef PVOID DLL_DIRECTORY_COOKIE; // fallback for older SDKs
#endif

// Fallbacks for older SDKs so this compiles on more Windows toolchains.
#ifndef DPI_AWARENESS_CONTEXT
typedef HANDLE DPI_AWARENESS_CONTEXT;
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#  define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// --- Prefer discrete GPU on hybrid laptops (hint; not guaranteed) ---
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                   = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance  = 1;
}

// NEW: centralize Windows path/CWD logic in one place (your existing helper).
#include "platform/win/PathUtilWin.h"
// NEW: Crash handler (minidumps) — initialize at process start in wWinMain.
#include "platform/win/CrashHandlerWin.h"

// === Fixed-timestep hookup (embedded fallback) ===
#include "colony/world/World.h"
#include "colony/loop/GameLoop.h"

namespace fs = std::filesystem;

// ---------- Utilities ----------
static std::wstring LastErrorMessage(DWORD err = GetLastError())
{
    LPWSTR msg = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
    std::wstring out = (len && msg) ? msg : L"";
    if (msg) LocalFree(msg);
    return out;
}

// Small helper for message boxes.
static void MsgBox(const std::wstring& title, const std::wstring& text, UINT flags = MB_ICONERROR | MB_OK)
{
    MessageBoxW(nullptr, text.c_str(), title.c_str(), flags);
}

// Fail‑fast on heap corruption for improved crash diagnosability.
static void EnableHeapTerminationOnCorruption()
{
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel)
    {
        using HeapSetInformation_t = BOOL (WINAPI *)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);
        auto pHeapSetInformation = reinterpret_cast<HeapSetInformation_t>(GetProcAddress(hKernel, "HeapSetInformation"));
        if (pHeapSetInformation)
        {
            pHeapSetInformation(GetProcessHeap(), HeapEnableTerminationOnCorruption, nullptr, 0);
        }
    }
}

// Restrict DLL search order to safe defaults and remove CWD from search path.
// Dynamically resolves SetDefaultDllDirectories for broad OS/SDK compatibility.
// Note: LOAD_LIBRARY_SEARCH_DEFAULT_DIRS sets the recommended base search order;
//       we *also* include LOAD_LIBRARY_SEARCH_USER_DIRS so AddDllDirectory()
//       entries (like .\D3D12 for Agility) apply process‑wide. (MS Docs)
static void EnableSafeDllSearch()
{
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32)
    {
        using PFN_SetDefaultDllDirectories = BOOL (WINAPI*)(DWORD);
        auto pSetDefaultDllDirectories =
            reinterpret_cast<PFN_SetDefaultDllDirectories>(GetProcAddress(hKernel32, "SetDefaultDllDirectories"));
        if (pSetDefaultDllDirectories)
        {
            // Include USER_DIRS so directories added via AddDllDirectory() participate in implicit loads.
            (void)pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        }

        // Explicitly add the application directory (defensive) and Agility folder ".\D3D12".
        using PFN_AddDllDirectory = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
        auto pAddDllDirectory =
            reinterpret_cast<PFN_AddDllDirectory>(GetProcAddress(hKernel32, "AddDllDirectory"));
        if (pAddDllDirectory)
        {
            const fs::path exeDir = winpath::exe_dir();
            if (!exeDir.empty())
            {
                (void)pAddDllDirectory(exeDir.c_str());

                // Agility SDK: place D3D12Core.dll (etc.) under "<exe>\D3D12".
                // Adding this directory ensures discovery with safe DLL search enabled.
                const fs::path agilityDir = exeDir / L"D3D12";
                if (fs::exists(agilityDir))
                {
                    (void)pAddDllDirectory(agilityDir.c_str());
                }
            }
        }
    }

    // Remove the current directory from the implicit DLL search path.
    // (Passing L"" removes CWD; passing NULL would restore legacy order.)
    SetDllDirectoryW(L"");
}

// High‑DPI awareness (Per‑Monitor‑V2 if available, else system‑DPI).
static void EnableHighDpiAwareness()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        using PFN_SetProcessDpiAwarenessContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSetCtx = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetCtx)
        {
            if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            {
                return;
            }
        }
        using PFN_SetProcessDPIAware = BOOL (WINAPI*)(void);
        auto pSetAware = reinterpret_cast<PFN_SetProcessDPIAware>(GetProcAddress(hUser32, "SetProcessDPIAware"));
        if (pSetAware) pSetAware(); // fallback to system DPI awareness
    }
}

// Simple file logger under %LOCALAPPDATA%\ColonyGame\logs (UTF‑16LE with BOM)
static fs::path LogsDir()
{
    fs::path out = winpath::writable_data_dir() / L"logs";
    std::error_code ec;
    fs::create_directories(out, ec);
    return out;
}

static std::wofstream OpenLogFile()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::wstringstream name;
    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";

    // Open in binary so we can explicitly write a UTF‑16 BOM for better editor compatibility.
    std::wofstream f(LogsDir() / name.str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (f)
    {
        const wchar_t bom = 0xFEFF; // UTF‑16LE BOM
        f.write(&bom, 1);
        f.flush();
    }
    return f;
}

// Friendly preflight checks for essential asset directories.
static bool CheckEssentialFiles()
{
    const wchar_t* const folders[] = { L"res", L"assets", L"shaders" };
    for (auto* folder : folders)
    {
        if (!fs::exists(folder))
        {
            std::wstring msg = L"Missing required folder: ";
            msg += folder;
            MsgBox(L"Colony Game - Startup Error", msg, MB_ICONERROR | MB_OK);
            return false;
        }
    }
    return true;
}

// Quote a single argument if it has spaces or quotes (Windows rules).
static std::wstring QuoteArg(const std::wstring& arg)
{
    bool needsQuotes = arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needsQuotes) return arg;
    std::wstring out = L"\"";
    for (wchar_t ch : arg)
    {
        if (ch == L'"') out += L"\\\"";
        else out += ch;
    }
    out += L"\"";
    return out;
}

// Build child command line from our own args (skip argv[0]).
static std::wstring BuildChildCommandLine(const std::wstring& childExe)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cmd = QuoteArg(childExe);
    for (int i = 1; i < argc; ++i)
    {
        cmd.push_back(L' ');
        cmd += QuoteArg(argv[i]);
    }
    if (argv) LocalFree(argv);
    return cmd;
}

#ifdef _DEBUG
// Prefer attaching to an existing parent console (if launched from a terminal).
static void AttachParentConsoleOrAlloc()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        AllocConsole();
    }
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
}
#endif

// ---- Single-instance guard ----
class SingleInstanceGuard {
public:
    SingleInstanceGuard() : h_(nullptr) {}
    ~SingleInstanceGuard() { if (h_) CloseHandle(h_); }
    bool acquire(const wchar_t* name) {
        h_ = CreateMutexW(nullptr, FALSE, name);
        return h_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
private:
    HANDLE h_;
};

#ifdef COLONY_EMBED_GAME_LOOP
// Forward decl; implemented at bottom.
static LRESULT CALLBACK EmbeddedWndProc(HWND, UINT, WPARAM, LPARAM);
static int RunEmbeddedGameLoop(std::wostream& log);
#endif

// ---------- Entry point ----------
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Initialize crash dumps as early as possible (Saved Games\Colony Game\Crashes).
    wincrash::InitCrashHandler(L"Colony Game"); // writes MiniDump via vectored handler. (MS docs)

    // Enable fail-fast behavior on heap corruption as early as possible.
    EnableHeapTerminationOnCorruption();

    // Constrain DLL search order before any loads and enable user dirs.
    EnableSafeDllSearch();

    // Ensure asset-relative paths work from any launch context (Explorer, VS, cmd).
    winpath::ensure_cwd_exe_dir(); // (0.1) working directory -> EXE folder

    // Avoid OS popups for missing DLLs, etc.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    // Make message boxes crisp under high DPI scaling. (0.2)
    EnableHighDpiAwareness();

#ifdef _DEBUG
    AttachParentConsoleOrAlloc();
#endif

    // Start logging.
    std::wofstream log = OpenLogFile();
    log << L"[Launcher] Colony Game Windows launcher starting.\n";

    // Single instance (0.2).
    SingleInstanceGuard guard;
    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF"))
    {
        MsgBox(L"Colony Game", L"Another instance is already running.");
        return 0;
    }

    // (0.3) Friendly preflight checks for folders users commonly misplace.
    if (!CheckEssentialFiles())
    {
        log << L"[Launcher] Preflight checks failed due to missing folders.\n";
        return 2;
    }

    // Build path to the game executable (same directory as the launcher).
    const fs::path exeDir  = fs::current_path();
    const fs::path gameExe = exeDir / L"ColonyGame.exe"; // keep in sync with your CMake target name
    if (!fs::exists(gameExe))
    {
        std::wstring msg = L"Could not find game executable at:\n";
        msg += gameExe.wstring();
        MsgBox(L"Colony Game - Startup Error", msg);
#ifdef COLONY_EMBED_GAME_LOOP
        log << L"[Launcher] EXE missing; falling back to embedded safe mode.\n";
        return RunEmbeddedGameLoop(log);
#else
        return 3;
#endif
    }

    // Prepare to spawn the game process.
    std::wstring cmd = BuildChildCommandLine(gameExe.wstring());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Note: CREATE_UNICODE_ENVIRONMENT is relevant only when lpEnvironment != nullptr; harmless otherwise.
    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE; // child does not inherit our error mode

    log << L"[Launcher] Spawning: " << cmd << L"\n";
    std::vector<wchar_t> cmdMutable(cmd.begin(), cmd.end());
    cmdMutable.push_back(L'\0');

    BOOL ok = CreateProcessW(
        gameExe.c_str(),          // lpApplicationName
        cmdMutable.data(),        // lpCommandLine (mutable!)
        nullptr, nullptr, FALSE,
        creationFlags,
        nullptr,                  // inherit our environment
        exeDir.c_str(),           // current directory for the child
        &si, &pi);

    if (!ok)
    {
        DWORD err = GetLastError();
        log << L"[Launcher] CreateProcessW failed (" << err << L"): " << LastErrorMessage(err) << L"\n";
#ifdef COLONY_EMBED_GAME_LOOP
        log << L"[Launcher] Falling back to embedded safe mode.\n";
        return RunEmbeddedGameLoop(log);
#else
        std::wstring msg = L"Failed to start game process.\n\nError "
                         + std::to_wstring(err) + L": " + LastErrorMessage(err);
        MsgBox(L"Colony Game", msg);
        return 3;
#endif
    }

    // Wait for the game to finish; return its exit code.
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    log << L"[Launcher] Game exited with code " << code << L"\n";
    return static_cast<int>(code);
}

#ifdef COLONY_EMBED_GAME_LOOP
// ========================== Embedded Safe-Mode Loop ==========================
//
// Provides a minimal fixed-timestep simulation + GDI visualization so players
// get *something* even if the main EXE can't launch. This avoids any D3D deps.
//
// Window proc draws the latest interpolated snapshot (positions of demo agents).
namespace {
    struct EmbeddedState {
        colony::RenderSnapshot snapshot;
    };
    static EmbeddedState g_state;
}

static LRESULT CALLBACK EmbeddedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(32, 32, 48));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 230));
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)SelectObject(dc, font);

        // World->screen transform (simple scale + center)
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const float scale = 60.0f;
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;

        // Draw agents
        HBRUSH agentBrush = CreateSolidBrush(RGB(80, 200, 255));
        HBRUSH oldBrush = (HBRUSH)SelectObject(dc, agentBrush);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(20, 120, 180));
        HPEN oldPen = (HPEN)SelectObject(dc, pen);

        for (const auto& p : g_state.snapshot.agent_positions)
        {
            const int x = (int)(cx + (float)p.x * scale);
            const int y = (int)(cy - (float)p.y * scale);
            const int r = 6;
            Ellipse(dc, x - r, y - r, x + r, y + r);
        }

        SelectObject(dc, oldPen);
        DeleteObject(pen);
        SelectObject(dc, oldBrush);
        DeleteObject(agentBrush);

        // HUD
        std::wstringstream hud;
        hud << L"Embedded Safe Mode  |  sim_step=" << g_state.snapshot.sim_step
            << L"  sim_time=" << std::fixed << std::setprecision(2) << g_state.snapshot.sim_time;
        TextOutW(dc, 8, 8, hud.str().c_str(), (int)hud.str().size());

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
    // 1) Create a basic Win32 window (no D3D) to visualize the sim via GDI.
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    const wchar_t* kClass = L"ColonyEmbeddedGameWindow";

    WNDCLASSW wc{};
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &EmbeddedWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;

    if (!RegisterClassW(&wc))
    {
        MsgBox(L"Colony Game", L"Failed to register embedded window class.");
        return 10;
    }

    HWND hwnd = CreateWindowExW(
        0, kClass, L"Colony Game (Embedded Safe Mode)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd)
    {
        MsgBox(L"Colony Game", L"Failed to create embedded window.");
        return 11;
    }

    // 2) Build the world and start the deterministic loop.
    colony::World world;
    colony::GameLoopConfig cfg{};
    cfg.fixed_dt = 1.0 / 60.0;
    cfg.max_frame_time = 0.25;
    cfg.max_updates_per_frame = 5;
    cfg.run_when_minimized = false;

    auto render = [&](const colony::World& w, float alpha) {
        g_state.snapshot = w.snapshot(alpha);
        // Ask the window to repaint using latest snapshot.
        InvalidateRect(hwnd, nullptr, FALSE);
    };

    log << L"[Embedded] Running fixed-timestep loop.\n";
    const int exitCode = colony::RunGameLoop(world, render, hwnd, cfg);

    DestroyWindow(hwnd);
    UnregisterClassW(kClass, hInst);
    return exitCode;
}
#endif // COLONY_EMBED_GAME_LOOP
