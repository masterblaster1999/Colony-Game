// platform/win/LauncherSpawnWin.cpp

#ifndef UNICODE
#   define UNICODE
#endif
#ifndef _UNICODE
#   define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include "platform/win/LauncherSpawnWin.h"

#include <windows.h>
#include <vector>

#include "platform/win/LauncherCliWin.h"
#include "platform/win/LauncherLoggingWin.h"
#include "platform/win/LauncherSystemWin.h"

namespace fs = std::filesystem;

namespace winlaunch
{
    SpawnResult SpawnAndWait(const fs::path&    gameExe,
                             const fs::path&    workingDir,
                             const std::wstring& childArgs,
                             std::wofstream&     log)
    {
        SpawnResult result{};

        // Include the quoted EXE as argv[0] in the child command line.
        std::wstring cmd = QuoteArgWindows(gameExe.wstring());
        if (!childArgs.empty())
        {
            cmd.push_back(L' ');
            cmd.append(childArgs);
        }

        STARTUPINFOW        si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);

        const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE;

        // Create mutable command-line buffer (Windows API requirement).
        std::vector<wchar_t> cmdMutable(cmd.begin(), cmd.end());
        cmdMutable.push_back(L'\0');

        WriteLog(
            log,
            L"[Launcher] Spawning: " + gameExe.wstring() +
            (childArgs.empty() ? L"" : L" " + childArgs)
        );

        const BOOL ok = ::CreateProcessW(
            gameExe.c_str(),        // lpApplicationName
            cmdMutable.data(),      // lpCommandLine (mutable buffer)
            nullptr,                // lpProcessAttributes
            nullptr,                // lpThreadAttributes
            FALSE,                  // bInheritHandles
            creationFlags,          // dwCreationFlags
            nullptr,                // lpEnvironment (inherit ours)
            workingDir.c_str(),     // lpCurrentDirectory
            &si,
            &pi
        );

        if (!ok)
        {
            const DWORD err = ::GetLastError();
            result.succeeded       = false;
            result.win32_error     = static_cast<uint32_t>(err);
            result.win32_error_text = LastErrorMessage(err);
            return result;
        }

        ::WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);

        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        result.succeeded = true;
        result.exit_code = static_cast<uint32_t>(code);
        return result;
    }
}
