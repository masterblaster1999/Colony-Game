#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>

static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = L'\0';
    return path;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Build command line: "<dir>\\ColonyGame.exe" [forwarded args...]
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring gamePath = GetExeDir() + L"\\ColonyGame.exe";
    std::wstring cmd = L"\"" + gamePath + L"\"";
    for (int i = 1; i < argc; ++i) { // forward arguments
        cmd += L" ";
        cmd += argv[i];
    }
    if (argv) LocalFree(argv);

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr,      // application (use command line path)
            cmd.data(),   // command line (mutable)
            nullptr, nullptr, FALSE,
            0, nullptr, GetExeDir().c_str(),
            &si, &pi)) {
        MessageBoxW(nullptr, L"Failed to spawn ColonyGame.exe", L"Colony Launcher",
                    MB_OK | MB_ICONERROR);
        return 2;
    }
    CloseHandle(pi.hThread);
    // Optionally wait; or return immediately and let game take over
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
