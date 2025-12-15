// src/platform/win/WinLauncher.cpp

#include "platform/win/WinCommon.h"
#include "platform/win/LauncherCliWin.h" // QuoteArgWindows()

#include <shellapi.h> // CommandLineToArgvW
#include <cwchar>    // wcsrchr
#include <string>
#include <vector>

static std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t* slash = ::wcsrchr(path, L'\\');
    if (slash)
        *slash = L'\0';

    return path;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // Build command line: "<dir>\\ColonyGame.exe" [forwarded args...]
    const std::wstring exeDir   = GetExeDir();
    const std::wstring gamePath = exeDir + L"\\ColonyGame.exe";

    // Quote argv[0] robustly (CommandLineToArgvW-compatible quoting rules).
    std::wstring cmd = QuoteArgWindows(gamePath);

    // Forward arguments using the shared quoting logic.
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv && argc > 1)
    {
        for (int i = 1; i < argc; ++i) // forward arguments
        {
            cmd.push_back(L' ');
            cmd.append(QuoteArgWindows(std::wstring(argv[i])));
        }
        ::LocalFree(argv);
        argv = nullptr;
    }
    else if (argv)
    {
        ::LocalFree(argv);
        argv = nullptr;
    }

    // CreateProcessW requires a mutable, NUL-terminated command line buffer.
    std::vector<wchar_t> cmdMutable(cmd.begin(), cmd.end());
    cmdMutable.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};

    if (!::CreateProcessW(
            gamePath.c_str(),      // lpApplicationName (explicit path)
            cmdMutable.data(),     // lpCommandLine (mutable)
            nullptr, nullptr,
            FALSE,
            0,
            nullptr,
            exeDir.c_str(),        // lpCurrentDirectory
            &si, &pi))
    {
        ::MessageBoxW(nullptr,
                      L"Failed to spawn ColonyGame.exe",
                      L"Colony Launcher",
                      MB_OK | MB_ICONERROR);
        return 2;
    }

    ::CloseHandle(pi.hThread);

    // Optionally wait; or return immediately and let game take over
    ::WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);

    ::CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
