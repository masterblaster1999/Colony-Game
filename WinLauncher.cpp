// WinLauncher.cpp - Robust Windows-only launcher for Colony-Game
// Build as a GUI subsystem (no console window). Unicode throughout.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shlwapi.h>    // PathRemoveFileSpecW, PathFileExistsW, PathIsRelativeW
#include <shellapi.h>   // CommandLineToArgvW, ShellExecuteExW
#include <shlobj.h>     // SHCreateDirectoryExW
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

// Prefer the discrete GPU on hybrid systems (NVIDIA/AMD).
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

HANDLE gMutex = nullptr;
HANDLE gJob   = nullptr;

std::wstring GetLastErrorMessage(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = (len && buf) ? std::wstring(buf, len) : L"(no message)";
    if (buf) LocalFree(buf);
    // Trim trailing CR/LF if present
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) msg.pop_back();
    return msg;
}

[[noreturn]] void FailBox(const wchar_t* title, const std::wstring& detail) {
    MessageBoxW(nullptr, detail.c_str(), title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
}

void SecureDllSearchOrder() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    using Fn = BOOL (WINAPI*)(DWORD);
    auto pSetDefaultDllDirectories =
        reinterpret_cast<Fn>(GetProcAddress(k32, "SetDefaultDllDirectories"));
    if (pSetDefaultDllDirectories) {
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    }
}

void SetDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32");
    if (user32) {
        using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto p = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware(); // fallback
}

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error",
                L"GetModuleFileNameW failed. Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }
    PathRemoveFileSpecW(path);
    return path;
}

bool Exists(const std::wstring& p) { return PathFileExistsW(p.c_str()) != FALSE; }

// --- Logging to %LOCALAPPDATA%\ColonyGame\logs\launcher.log -------------------

std::wstring GetLocalAppData() {
    wchar_t buf[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return L"";
    return std::wstring(buf);
}

void EnsureDirRecursive(const std::wstring& dir) {
    // CreateDirectoryEx handles intermediate segments; returns non-zero on success or already exists.
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
}

std::wstring LogsDir() {
    std::wstring root = GetLocalAppData();
    if (root.empty()) return L"";
    std::wstring d = root + L"\\ColonyGame\\logs";
    EnsureDirRecursive(d);
    return d;
}

void Log(const wchar_t* fmt, ...) {
    std::wstring dir = LogsDir();
    if (dir.empty()) return;
    std::wstring path = dir + L"\\launcher.log";

    wchar_t line[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);

    if (FILE* f = _wfopen(path.c_str(), L"a+, ccs=UTF-8")) {
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(f, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line);
        fclose(f);
    }
}

// --- Config/Target discovery --------------------------------------------------

bool ReadLauncherTargetFromIni(const std::wstring& moduleDir, std::wstring& outExeRel) {
    // Optional config: moduleDir\launcher.ini with line: target=RelativeOrAbsolutePathToExe
    std::wstring ini = moduleDir + L"\\launcher.ini";
    if (!PathFileExistsW(ini.c_str())) return false;

    std::wifstream f(ini);
    if (!f) return false;
    std::wstring line;
    while (std::getline(f, line)) {
        const std::wstring key = L"target=";
        if (line.rfind(key, 0) == 0 && line.size() > key.size()) {
            outExeRel = line.substr(key.size());
            // trim spaces
            size_t a = outExeRel.find_first_not_of(L" \t");
            size_t b = outExeRel.find_last_not_of(L" \t\r\n");
            if (a == std::wstring::npos) return false;
            outExeRel = outExeRel.substr(a, b - a + 1);
            return !outExeRel.empty();
        }
    }
    return false;
}

std::wstring FindGameExe(const std::wstring& moduleDir) {
    // 1) user-configured target
    std::wstring rel;
    if (ReadLauncherTargetFromIni(moduleDir, rel)) {
        std::wstring abs = rel;
        if (!PathIsRelativeW(rel.c_str())) {
            if (Exists(abs)) return abs;
        } else {
            abs = moduleDir + L"\\" + rel;
            if (Exists(abs)) return abs;
        }
    }
    // 2) common defaults (adjust if your actual exe name differs)
    const wchar_t* candidates[] = {
        L"ColonyGame.exe",
        L"Colony-Game.exe",
        L"Colony.exe",
        L"Game.exe",
        L"bin\\ColonyGame.exe",
        L"build\\ColonyGame.exe"
    };
    for (auto c : candidates) {
        std::wstring abs = moduleDir + L"\\" + c;
        if (Exists(abs)) return abs;
    }
    // 3) last resort: first *.exe next to the launcher that is not the launcher itself
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((moduleDir + L"\\*.exe").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            // Avoid self by skipping files containing "Launcher" in the name
            if (wcsstr(fd.cFileName, L"Launcher") == nullptr) {
                std::wstring abs = moduleDir + L"\\" + fd.cFileName;
                if (Exists(abs)) { FindClose(h); return abs; }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return L"";
}

// --- Argument quoting (args-only string, robust Windows rules) ----------------

void AppendQuoted(std::wstring& out, const std::wstring& s) {
    bool needQuotes = s.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needQuotes) { out.append(s); return; }
    out.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t ch : s) {
        if (ch == L'\\') {
            backslashes++;
            out.push_back(L'\\');
        } else if (ch == L'"') {
            // escape all preceding backslashes, then the quote itself
            out.append(backslashes + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.push_back(ch);
            backslashes = 0;
        }
    }
    // escape trailing backslashes before closing quote
    if (backslashes) out.append(backslashes, L'\\');
    out.push_back(L'"');
}

std::wstring BuildArgsTail(int argc, wchar_t** argv) {
    std::wstring out;
    for (int i = 1; i < argc; ++i) {
        if (!out.empty()) out.push_back(L' ');
        AppendQuoted(out, argv[i]);
    }
    return out;
}

// --- Job object: kill child if launcher dies ---------------------------------

bool SetupKillOnCloseJob() {
    gJob = CreateJobObjectW(nullptr, nullptr);
    if (!gJob) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return !!SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
}

// --- Elevated fallback using ShellExecuteEx (wait + exit code) ----------------

bool TryElevatedLaunch(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, DWORD& exitCodeOut) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = nullptr;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.lpDirectory = cwd.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD e = GetLastError();
        // User canceled UAC = ERROR_CANCELLED; treat as failure but with friendly message
        Log(L"ShellExecuteExW(runas) failed: %lu (%s)", e, GetLastErrorMessage(e).c_str());
        return false;
    }

    if (sei.hProcess) {
        if (gJob) {
            // Assign may fail for elevated child; ignore error but log.
            if (!AssignProcessToJobObject(gJob, sei.hProcess)) {
                Log(L"AssignProcessToJobObject (elevated) failed: %lu", GetLastError());
            }
        }
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        exitCodeOut = code;
    } else {
        // No handle; best we can do is succeed without exit code.
        exitCodeOut = 0;
    }
    return true;
}

// --- Core launch routine ------------------------------------------------------

bool LaunchGame(const std::wstring& exePath,
                const std::wstring& argsTail,
                const std::wstring& workingDir,
                DWORD& childExitCode)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT; // GUI game; no console needed

    // Windows requires a mutable buffer for lpCommandLine if non-null.
    std::vector<wchar_t> mutableArgs(argsTail.begin(), argsTail.end());
    mutableArgs.push_back(L'\0');

    SecureDllSearchOrder();

    BOOL ok = CreateProcessW(
        exePath.c_str(),                  // lpApplicationName
        mutableArgs.data(),               // lpCommandLine (args-only)
        nullptr, nullptr, FALSE,
        flags,
        nullptr,                          // inherit environment
        workingDir.c_str(),               // working directory
        &si, &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        Log(L"CreateProcessW failed (%lu: %s) for: %s", e, GetLastErrorMessage(e).c_str(), exePath.c_str());

        if (e == ERROR_ELEVATION_REQUIRED || e == ERROR_ACCESS_DENIED) {
            Log(L"Attempting elevated relaunch via ShellExecuteExW(runas)...");
            if (TryElevatedLaunch(exePath, argsTail, workingDir, childExitCode)) {
                Log(L"Elevated launch completed with code %lu", (unsigned long)childExitCode);
                return true;
            }
        }

        std::wstring detail = L"Failed to start the game.\n\n"
                              L"Executable: " + exePath + L"\n\n" +
                              L"Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e) + L"\n\n"
                              L"Check %LOCALAPPDATA%\\ColonyGame\\logs\\launcher.log for details.";
        FailBox(L"Launch Failed", detail);
    }

    // Ensure child dies if the launcher dies
    if (gJob) {
        if (!AssignProcessToJobObject(gJob, pi.hProcess)) {
            Log(L"AssignProcessToJobObject failed: %lu", GetLastError());
        }
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    childExitCode = exitCode;
    Log(L"Game exited with code %lu", (unsigned long)exitCode);
    return true;
}

} // namespace

// GUI subsystem entry point
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetDpiAwareness();

    // Single instance (Global so it also covers elevated/non-elevated mix)
    gMutex = CreateMutexW(nullptr, TRUE, L"Global\\ColonyGame_SingleInstance");
    if (!gMutex) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error", L"CreateMutexW failed. Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        return 0;
    }

    std::wstring moduleDir = GetModuleDir();
    if (!SetCurrentDirectoryW(moduleDir.c_str())) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error",
                L"SetCurrentDirectoryW(\"" + moduleDir + L"\") failed. Error " +
                std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }

    if (!SetupKillOnCloseJob()) {
        Log(L"Create JobObject (KILL_ON_JOB_CLOSE) failed: %lu", GetLastError());
    }

    std::wstring gameExe = FindGameExe(moduleDir);
    if (gameExe.empty()) {
        FailBox(L"Launcher Error",
                L"Could not locate the game executable next to the launcher.\n\n"
                L"Create launcher.ini with a line like:\n    target=bin\\ColonyGame.exe");
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring argsTail = (argv ? BuildArgsTail(argc, argv) : L"");
    if (argv) LocalFree(argv);

    DWORD childExitCode = 0;
    const bool ok = LaunchGame(gameExe, argsTail, moduleDir, childExitCode);

    if (gJob)   { CloseHandle(gJob);   gJob = nullptr; } // Will kill child if it's still running
    if (gMutex) { ReleaseMutex(gMutex); CloseHandle(gMutex); gMutex = nullptr; }

    return ok ? static_cast<int>(childExitCode) : 1;
}
