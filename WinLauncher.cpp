// WinLauncher.cpp — Unicode‑safe, single‑instance Windows bootstrapper
// Build: Windows only, C++17 (std::filesystem)
//
// Key behaviors:
//  1) Forces CWD = EXE directory (via winpath::ensure_cwd_exe_dir), so relative assets (res/) load reliably.
//  2) Rebuilds the child's command-line with correct Windows quoting rules.
//  3) Launches the real game EXE via CreateProcessW with lpApplicationName set.
//  4) Enforces single-instance via a named mutex.
//  5) Verifies a sibling res/ directory exists; logs diagnostics to %LOCALAPPDATA%.
//
// References:
//  - CreateProcessW: https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
//  - GetCommandLineW: https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getcommandlinew
//  - CommandLineToArgvW: https://learn.microsoft.com/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
//  - Known folders (used inside PathUtilWin.cpp): https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath

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

// NEW: centralize Windows path/CWD logic in one place.
#include "platform/win/PathUtilWin.h"

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

// ---------- Entry point ----------
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Ensure asset-relative paths work from any launch context (Explorer, VS, cmd).
    winpath::ensure_cwd_exe_dir(); // <-- patch applied: call once at the very top

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX); // cleaner UX on missing DLLs, etc.

    SingleInstanceGuard guard;
    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF")) {
        MsgBox(L"Colony Game", L"Another instance is already running.");
        return 0;
    }

    auto exeDir = winpath::exe_dir();
    auto log = OpenLogFile();
    log << L"[Launcher] started in: " << exeDir.wstring() << L"\n";

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

    BOOL ok = CreateProcessW(
        gameExe.wstring().c_str(),                    // lpApplicationName (do not quote paths here)
        cmdline.empty() ? nullptr : cmdline.data(),   // lpCommandLine (only args, properly quoted)
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT,
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
}
