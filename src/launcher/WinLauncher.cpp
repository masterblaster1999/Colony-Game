// WinLauncher.cpp — Unicode-safe, single-instance Windows bootstrapper (Windows-only)

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
#include <winerror.h>   // SUCCEEDED, HRESULT helpers
#include <shellapi.h>   // CommandLineToArgvW
#include <shlobj.h>     // SHGetKnownFolderPath
#include <objbase.h>    // CoTaskMemFree
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>

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
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return out;
}

static fs::path ExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return fs::path(buf);
}

static fs::path ExeDir() {
    auto p = ExePath();
    return p.empty() ? fs::path() : p.parent_path();
}

static void EnsureWorkingDirectoryIsExeDir() {
    auto dir = ExeDir();
    if (!dir.empty()) SetCurrentDirectoryW(dir.c_str());
}

static std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needQuotes = false;
    for (wchar_t c : arg) if (iswspace(c) || c == L'"') { needQuotes = true; break; }
    if (!needQuotes) return arg;

    std::wstring result; result.push_back(L'"');
    size_t i = 0;
    while (i < arg.size()) {
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }
        if (i == arg.size()) { result.append(bs * 2, L'\\'); break; }
        if (arg[i] == L'"') { result.append(bs * 2 + 1, L'\\'); result.push_back(L'"'); ++i; }
        else { result.append(bs, L'\\'); result.push_back(arg[i++]); }
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
    std::tm tm{}; localtime_s(&tm, &t);

    std::wstringstream name;
    name << std::put_time(&tm, L"%Y%m%d-%H%M%S") << L".log";
    auto path = LogsDir() / name.str();

    std::wofstream f(path, std::ios::out | std::ios::trunc);
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

static fs::path ResolveGameExe(const fs::path& baseDir) {
    std::error_code ec;
    auto cfg = baseDir / L"res" / L"launcher.cfg";
    if (fs::exists(cfg, ec)) {
        std::wifstream fin(cfg);
        std::wstring name;
        if (fin && std::getline(fin, name)) {
            while (!name.empty() && (name.back() == L'\r' || name.back() == L'\n')) name.pop_back();
            fs::path cand = baseDir / name;
            if (fs::exists(cand, ec)) return cand;
        }
    }
    const wchar_t* candidates[] = { L"ColonyGame.exe", L"Colony-Game.exe", L"Game.exe" };
    for (auto* n : candidates) {
        fs::path p = baseDir / n;
        if (fs::exists(p, ec)) return p;
    }
    return {};
}

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
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    EnsureWorkingDirectoryIsExeDir();

    SingleInstanceGuard guard;
    if (!guard.acquire(L"Global\\ColonyGame_Singleton_1E2D13F1_B96C_471B_82F5_829B0FF5D4AF")) {
        MsgBox(L"Colony Game", L"Another instance is already running.");
        return 0;
    }

    auto log = OpenLogFile();
    log << L"[Launcher] started in: " << ExeDir().wstring() << L"\n";

    if (!VerifyResources(ExeDir())) {
        MsgBox(L"Colony Game",
               L"Missing or invalid 'res' folder next to the executable.\n"
               L"Make sure the game is installed correctly.");
        log << L"[Launcher] res/ check failed\n";
        return 1;
    }

    auto gameExe = ResolveGameExe(ExeDir());
    if (gameExe.empty()) {
        MsgBox(L"Colony Game",
               L"Could not find the game executable next to the launcher.\n"
               L"Looked for 'ColonyGame.exe', 'Colony-Game.exe', or 'Game.exe'.\n"
               L"You can override via 'res/launcher.cfg'.");
        log << L"[Launcher] no child EXE found\n";
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring tail = BuildCmdLineTail(argc, argv);
    if (argv) LocalFree(argv);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdline = tail;

    log << L"[Launcher] launching: " << gameExe.wstring() << L"  args: " << tail << L"\n";

    BOOL ok = CreateProcessW(
        gameExe.c_str(), // lpApplicationName
        cmdline.empty() ? nullptr : cmdline.data(),
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        ExeDir().c_str(),
        &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        std::wstring msg = L"Failed to start game process.\n\nError " +
                           std::to_wstring(err) + L": " + LastErrorMessage(err);
        MsgBox(L"Colony Game", msg);
        log << L"[Launcher] CreateProcessW failed: " << err << L" : " << LastErrorMessage(err) << L"\n";
        return 2;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log << L"[Launcher] success; exiting.\n";
    return 0;
}
