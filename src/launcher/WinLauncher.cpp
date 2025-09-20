// Windows-only launcher: single instance, safe DLL search, Unicode-safe process spawn.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>

#include "src/win/SingleInstance.h"    // already in repo
#include "src/win/WinUtils.h"          // GetLastErrorMessage, TryHardenDllSearch
#include "platform/win/WinPaths.hpp"   // winenv::exe_dir(), resource_dir(), etc.
#include "LauncherConfig.h"

using namespace std;
namespace fs = std::filesystem;

static wstring quote(const wstring& s) {
    // Minimal robust quoting for CreateProcessW (cmdline)
    if (s.find_first_of(L" \t\"") == wstring::npos) return s;
    wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'\"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

int wmain(int argc, wchar_t** argv) {
    // 1) QoL and security first
    TryHardenDllSearch();                             // centralized
    const fs::path exeDir = winenv::exe_dir();        // centralized
    SetCurrentDirectoryW(exeDir.c_str());             // relative paths behave

    // 2) Single-instance guard (existing helper)
    SingleInstance inst(L"ColonyGame.SingleInstance");
    if (inst.already_running()) {
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 3) Determine which EXE to launch (via res/launcher.cfg)
    const wstring targetName = launcher::read_target_exe();
    const fs::path targetExe = exeDir / targetName;

    // 4) Build command line (quoted)
    wstring cmd = quote(targetExe.wstring());
    for (int i = 1; i < argc; ++i) {
        cmd += L" ";
        cmd += quote(argv[i]);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        targetExe.c_str(),
        cmd.data(),           // mutable buffer OK; CreateProcessW may write here
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        exeDir.c_str(),       // start in exe directory
        &si, &pi
    );

    if (!ok) {
        const wstring msg = L"Failed to start " + targetExe.wstring() +
                            L"\n\nWindows error: " + GetLastErrorMessage(GetLastError());
        MessageBoxW(nullptr, msg.c_str(), L"Colony-Game Launcher", MB_OK | MB_ICONERROR);
        return (int)GetLastError();
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}
