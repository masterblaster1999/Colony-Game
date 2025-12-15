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
#include <jobapi2.h>
#include <string>
#include <vector>

#include "platform/win/LauncherCliWin.h"
#include "platform/win/LauncherLoggingWin.h"
#include "platform/win/LauncherSystemWin.h"

namespace fs = std::filesystem;

namespace winlaunch
{
    SpawnResult SpawnAndWait(const fs::path&     gameExe,
                             const fs::path&     workingDir,
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

        // ---------------------------------------------------------------------
        // Ensure the spawned game process is terminated if *this* launcher dies.
        //
        // We place the child process into a Job Object configured with
        // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, and keep the job handle alive for
        // the lifetime of this function. If the launcher is killed/crashes, the
        // OS closes the handle → all processes in the job are terminated.
        //
        // Note: If the launcher is itself inside a restrictive job, assigning a
        // child to our job may fail. In that case we log a warning and proceed
        // without the kill-on-launcher-exit behavior.
        // ---------------------------------------------------------------------
        HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
        if (job)
        {
            // Defensive: ensure the job handle is not inheritable.
            ::SetHandleInformation(job, HANDLE_FLAG_INHERIT, 0);

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

            if (!::SetInformationJobObject(job,
                                           JobObjectExtendedLimitInformation,
                                           &jeli,
                                           static_cast<DWORD>(sizeof(jeli))))
            {
                const DWORD err = ::GetLastError();
                WriteLog(
                    log,
                    L"[Launcher] WARNING: SetInformationJobObject(KILL_ON_JOB_CLOSE) failed (" +
                    std::to_wstring(err) + L"): " + LastErrorMessage(err)
                );

                ::CloseHandle(job);
                job = nullptr;
            }
        }
        else
        {
            const DWORD err = ::GetLastError();
            WriteLog(
                log,
                L"[Launcher] WARNING: CreateJobObjectW failed (" +
                std::to_wstring(err) + L"): " + LastErrorMessage(err)
            );
        }

        // Create the child process suspended so we can associate it with the job
        // *before* it begins executing.
        const DWORD creationFlags =
            CREATE_UNICODE_ENVIRONMENT |
            CREATE_DEFAULT_ERROR_MODE |
            CREATE_SUSPENDED;

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
            result.succeeded        = false;
            result.win32_error      = static_cast<uint32_t>(err);
            result.win32_error_text = LastErrorMessage(err);

            if (job)
            {
                ::CloseHandle(job);
            }

            return result;
        }

        // Associate the process with the job before it starts running.
        if (job)
        {
            if (!::AssignProcessToJobObject(job, pi.hProcess))
            {
                const DWORD err = ::GetLastError();
                WriteLog(
                    log,
                    L"[Launcher] WARNING: AssignProcessToJobObject failed (" +
                    std::to_wstring(err) + L"): " + LastErrorMessage(err)
                );

                // Kill-on-launcher-exit won't work in this case; don't keep job handle.
                ::CloseHandle(job);
                job = nullptr;
            }
        }

        // Start the child now that it is (ideally) in the job.
        const DWORD resumeResult = ::ResumeThread(pi.hThread);
        if (resumeResult == static_cast<DWORD>(-1))
        {
            const DWORD err = ::GetLastError();
            WriteLog(
                log,
                L"[Launcher] ERROR: ResumeThread failed (" +
                std::to_wstring(err) + L"): " + LastErrorMessage(err)
            );

            // Avoid deadlocking forever on a process that never actually starts.
            ::TerminateProcess(pi.hProcess, 1);
            ::WaitForSingleObject(pi.hProcess, 5000);

            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);

            if (job)
            {
                ::CloseHandle(job);
            }

            result.succeeded        = false;
            result.win32_error      = static_cast<uint32_t>(err);
            result.win32_error_text = LastErrorMessage(err);
            return result;
        }

        ::WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);

        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        if (job)
        {
            ::CloseHandle(job);
        }

        result.succeeded = true;
        result.exit_code = static_cast<uint32_t>(code);
        return result;
    }
}
