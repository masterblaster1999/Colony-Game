// WinLauncher.cpp - Minimal, robust Windows-only launcher for Colony-Game
// Build as a GUI subsystem (no console window). Unicode throughout.

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlwapi.h>   // PathRemoveFileSpecW, PathFileExistsW
#include <shellapi.h>  // CommandLineToArgvW, ShellExecuteW
#include <string>
#include <vector>
#include <fstream>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

std::wstring GetLastErrorMessage(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = (len && buf) ? std::wstring(buf, len) : L"(no message)";
    if (buf) LocalFree(buf);
    return msg;
}

[[noreturn]] void FailBox(const wchar_t* title, const std::wstring& detail) {
    MessageBoxW(nullptr, detail.c_str(), title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
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

bool ReadLauncherTargetFromIni(const std::wstring& moduleDir, std::wstring& outExeRel) {
    // Optional config: moduleDir\launcher.ini with line: target=RelativeOrAbsolutePathToExe
    std::wstring ini = moduleDir + L"\\launcher.ini";
    if (!PathFileExistsW(ini.c_str())) return false;

    std::wifstream f(ini);
    if (!f) return false;
    std::wstring line;
    while (std::getline(f, line)) {
        // very small parser
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

bool Exists(const std::wstring& p) { return PathFileExistsW(p.c_str()) != FALSE; }

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
    // 2) common defaults (adjust list if your actual exe name differs)
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
    // 3) last resort: pick the first *.exe next to the launcher that is not the launcher itself
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((moduleDir + L"\\*.exe").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            // Avoid self by comparing filename prefix "ColonyLauncher"
            if (wcsstr(fd.cFileName, L"Launcher") == nullptr) {
                std::wstring abs = moduleDir + L"\\" + fd.cFileName;
                if (Exists(abs)) { FindClose(h); return abs; }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return L"";
}

std::wstring RebuildArgs(int argc, wchar_t** argv) {
    // Rebuild quoted arguments excluding argv[0]
    std::wstring out;
    for (int i = 1; i < argc; ++i) {
        if (!out.empty()) out += L' ';
        out += L"\"";
        for (wchar_t ch : std::wstring(argv[i])) {
            if (ch == L'"') out += L"\\\"";
            else out += ch;
        }
        out += L"\"";
    }
    return out;
}

} // namespace

// GUI subsystem entry point
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Single instance (Global so it also covers elevated/non-elevated mix)
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\ColonyGame_SingleInstance");
    if (!hMutex) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error", L"CreateMutexW failed. Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    std::wstring moduleDir = GetModuleDir();

    if (!SetCurrentDirectoryW(moduleDir.c_str())) {
        DWORD e = GetLastError();
        FailBox(L"Launcher Error",
                L"SetCurrentDirectoryW(\"" + moduleDir + L"\") failed. Error " +
                std::to_wstring(e) + L": " + GetLastErrorMessage(e));
    }

    std::wstring gameExe = FindGameExe(moduleDir);
    if (gameExe.empty()) {
        FailBox(L"Launcher Error",
                L"Could not locate the game executable next to the launcher.\n\n"
                L"Create launcher.ini with a line like:\n    target=bin\\ColonyGame.exe");
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring argTail = (argv ? RebuildArgs(argc, argv) : L"");
    if (argv) LocalFree(argv);

    std::wstring cmd = L"\"" + gameExe + L"\"" + (argTail.empty() ? L"" : L" " + argTail);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Start in the module directory; no console window because this is a GUI app
    BOOL ok = CreateProcessW(
        nullptr,                         // application name (use command line)
        cmd.data(),                      // command line (mutable buffer)
        nullptr, nullptr, FALSE,
        0,                               // no special flags
        nullptr,                         // inherit environment
        moduleDir.c_str(),               // working directory
        &si, &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        // If elevation is required, try ShellExecuteW with "runas" to show UAC
        if (e == ERROR_ELEVATION_REQUIRED) {
            HINSTANCE sh = ShellExecuteW(nullptr, L"runas", gameExe.c_str(),
                                         argTail.empty() ? nullptr : argTail.c_str(),
                                         moduleDir.c_str(), SW_SHOWNORMAL);
            if ((UINT_PTR)sh > 32) return 0;
        }
        FailBox(L"Launch Failed",
                L"CreateProcessW failed launching:\n  " + gameExe + L"\n\n"
                L"Error " + std::to_wstring(e) + L": " + GetLastErrorMessage(e) + L"\n\n"
                L"Command line was:\n  " + cmd);
    }

    // We immediately detach; the game owns its lifetime.
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hMutex);
    return 0;
}
