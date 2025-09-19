// WinLauncher.cpp  — Unicode-safe, single-instance Windows bootstrapper with crash dumps
// Build: Windows only. Requires C++17 (std::filesystem).
// This file replaces the previous WinLauncher.cpp and absorbs SingleClick duties.

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
#include <shellapi.h>      // CommandLineToArgvW
#include <shlobj.h>        // SHGetKnownFolderPath
#include <knownfolders.h>  // FOLDERID_LocalAppData
#include <objbase.h>       // CoTaskMemFree
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cwctype>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

#include "platform/win/CrashHandler.hpp"  // cg::win::CrashHandler

namespace fs = std::filesystem;

// ---------- Utilities ----------

// Format the last Win32 error into a readable string.
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

// LONG-PATH FIX: robustly get our own module path with a growable buffer.
// GetModuleFileNameW returns the number of characters written (excl. NUL).
// If the buffer is too small it returns the buffer size and sets ERROR_INSUFFICIENT_BUFFER.
// We loop and grow until it fits.
static std::wstring GetModulePathW() {
    // Start reasonably; grow as needed.
    std::wstring buf(512, L'\0');

    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            // Hard failure; propagate a useful message to the user later.
            std::wstring emsg = L"GetModuleFileNameW failed: " + LastErrorMessage();
            MessageBoxW(nullptr, emsg.c_str(), L"Colony Game", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return std::wstring();
        }

        // Success if we clearly fit inside the buffer.
        if (n < buf.size() - 1) {  // leave room for NUL
            buf.resize(n);
            return buf;
        }

        // Borderline or truncated: grow and try again.
        if (buf.size() >= 32768) { // NT path limit (~32K)
            MessageBoxW(nullptr, L"Executable path exceeds ~32K characters.", L"Colony Game",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return std::wstring();
        }
        buf.resize(buf.size() * 2);
    }
}

// Convert an absolute path to an extended-length path (\\?\ or \\?\UNC\...) if needed.
// We only add the prefix when the string is long enough to risk MAX_PATH issues
// and we avoid duplicating the prefix if it is already present.
static std::wstring ToExtendedIfNeeded(const std::wstring& absPath) {
    auto has_prefix = [](const std::wstring& s, const wchar_t* pre) {
        return s.rfind(pre, 0) == 0;
    };

    if (absPath.empty() ||
        has_prefix(absPath, LR"(\\?\)") ||
        has_prefix(absPath, LR"(\\.\)")) {
        return absPath;
    }

    // Add extended prefix only when near or beyond MAX_PATH.
    if (absPath.size() >= MAX_PATH) {
        // Drive-letter path?  e.g. "C:\foo\bar"
        if (absPath.size() >= 2 && absPath[1] == L':') {
            return LR"(\\?\)" + absPath;
        }
        // UNC path? e.g. "\\server\share\dir"
        if (has_prefix(absPath, LR"(\\)")) {
            return LR"(\\?\UNC\)" + absPath.substr(2);
        }
    }
    return absPath;
}

static fs::path ExePath() {
    std::wstring p = GetModulePathW(); // LONG-PATH FIX: dynamic retrieval
    return p.empty() ? fs::path() : fs::path(p);
}

static fs::path ExeDir() {
    auto p = ExePath();
    return p.empty() ? fs::path() : p.parent_path();
}

// Trim leading/trailing Unicode whitespace.
static std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(static_cast<unsigned>(s[b]))) ++b;
    while (e > b && iswspace(static_cast<unsigned>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Remove surrounding quotes if present.
static std::wstring Unquote(const std::wstring& s) {
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Build a proper Windows command line from argv[1..] following CommandLineToArgvW rules.
static std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needQuotes = false;
    for (wchar_t c : arg) {
        if (iswspace(static_cast<unsigned>(c)) || c == L'"') { needQuotes = true; break; }
    }
    if (!needQuotes) return arg;

    std::wstring result; result.push_back(L'"');
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
    PWSTR path = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
        out = fs::path(path) / L"ColonyGame" / L"logs";
        CoTaskMemFree(path);
    }
    std::error_code ec;
    fs::create_directories(out, ec);
    return out;
}

static std::wofstream OpenLogFile() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_MSC_VER)
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif

    std::wstringstream name;
    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";
    auto path = LogsDir() / name.str();

    std::wofstream f(path, std::ios::out | std::ios::trunc);
    if (f) {
        // Proper UTF-16LE BOM for Notepad readability.
        const wchar_t bom = 0xFEFF;
        f.put(bom);
    }
    return f;
}

static void MsgBox(const std::wstring& title, const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static bool VerifyResources(const fs::path& root) {
    auto res = root / L"res";
    std::error_code ec;
    return fs::exists(res, ec) && fs::is_directory(res, ec);
}

// Find the child game EXE next to the launcher.
// Try common names; optionally let res/launcher.cfg override.
static fs::path ResolveGameExe(const fs::path& baseDir) {
    // Optional override via config file: res/launcher.cfg (first non-empty line is exe name or path)
    std::error_code ec;
    auto cfg = baseDir / L"res" / L"launcher.cfg";
    if (fs::exists(cfg, ec)) {
        std::wifstream fin(cfg);
        std::wstring line;
        while (fin && std::getline(fin, line)) {
            line = Trim(Unquote(line));
            if (!line.empty()) {
                fs::path cand(line);
                if (cand.is_relative()) cand = baseDir / cand;
                if (fs::exists(cand, ec)) return cand;
                break; // First non-empty line is authoritative; if missing, fall through to defaults.
            }
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
// LONG-PATH FIXES applied in ExePath() and before CreateProcessW() below.
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // 1) Install crash handler ASAP (also normalizes CWD to the EXE folder).
    cg::win::CrashHandler::Install(L"ColonyGame", /*fixWorkingDir=*/true, /*showMessageBox=*/true);

    // 2) Hide noisy OS error UI for a smoother user experience.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);

    // 3) Single instance
    SingleInstanceGuard guard;
    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF")) {
        MsgBox(L"Colony Game", L"Another instance is already running.");
        return 0;
    }

    auto exeDir = ExeDir();
    auto log = OpenLogFile();
    if (log) log << L"[Launcher] started in: " << exeDir.wstring() << L"\n";

    if (!VerifyResources(exeDir)) {
        MsgBox(L"Colony Game",
               L"Missing or invalid 'res' folder next to the executable.\n"
               L"Make sure the game is installed correctly.");
        if (log) log << L"[Launcher] res/ check failed\n";
        return 1;
    }

    // Resolve child game EXE
    auto gameExe = ResolveGameExe(exeDir);
    if (gameExe.empty()) {
        MsgBox(L"Colony Game",
               L"Could not find the game executable next to the launcher.\n"
               L"Looked for 'ColonyGame.exe', 'Colony-Game.exe', or 'Game.exe'.\n"
               L"You can override via 'res/launcher.cfg'.");
        if (log) log << L"[Launcher] no child EXE found\n";
        return 1;
    }

    // Build arguments *without* embedding exe path (pass exe via lpApplicationName).
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring tail = BuildCmdLineTail(argc, argv);
    if (argv) LocalFree(argv);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf; // mutable buffer for CreateProcessW
    LPWSTR cmdLinePtr = nullptr;
    if (!tail.empty()) {
        cmdBuf.assign(tail.begin(), tail.end());
        cmdBuf.push_back(L'\0');
        cmdLinePtr = cmdBuf.data();
    }

    // LONG-PATH FIX: use extended-length prefixes for CreateProcessW only when necessary.
    const std::wstring gameExeStr = gameExe.wstring();
    const std::wstring cwdStr     = exeDir.wstring();
    const std::wstring appName    = ToExtendedIfNeeded(gameExeStr);
    const std::wstring workDir    = ToExtendedIfNeeded(cwdStr);

    if (log) log << L"[Launcher] launching: " << gameExeStr << L"  args: " << tail << L"\n";

    BOOL ok = CreateProcessW(
        appName.c_str(),               // lpApplicationName (absolute path; do not quote)
        cmdLinePtr,                    // lpCommandLine (only args, properly quoted; must be mutable or null)
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        workDir.c_str(),               // ensure correct CWD for assets (also set by CrashHandler)
        &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        std::wstring msg = L"Failed to start game process.\n\n" +
                           std::wstring(L"Error ") + std::to_wstring(err) + L": " + LastErrorMessage(err);
        MsgBox(L"Colony Game", msg);
        if (log) log << L"[Launcher] CreateProcessW failed: " << err << L" : " << LastErrorMessage(err) << L"\n";
        return 2;
    }

    // Clean up quickly; let the game own the lifetime
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (log) log << L"[Launcher] success; exiting.\n";
    return 0;
}
