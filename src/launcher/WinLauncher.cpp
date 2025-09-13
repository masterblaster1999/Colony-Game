// src/launcher/WinLauncher.cpp
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <shlwapi.h>    // PathRemoveFileSpecW, PathAppendW, PathFileExistsW
#include <dbghelp.h>    // MiniDumpWriteDump
#include <string>
#include <cstdarg>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Dbghelp.lib")

// Prefer the discrete GPU on hybrid systems (NVIDIA Optimus / AMD PowerXpress).
// (These exports are read by GPU drivers to select the high-performance GPU.)
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}

// Allow overriding the game exe name at compile time (CMake sets this).
#ifndef COLONY_GAME_EXE
#define COLONY_GAME_EXE L"ColonyGame.exe"
#endif

static HANDLE gMutex = nullptr;

static void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    ::CreateDirectoryW(L"logs", nullptr);
    if (FILE* f = _wfopen(L"logs\\launcher.log", L"a+, ccs=UTF-8")) {
        SYSTEMTIME st; ::GetLocalTime(&st);
        fwprintf(f, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buf);
        fclose(f);
    }
}

static void ForceWorkingDirectoryToExeDir()
{
    wchar_t path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    ::PathRemoveFileSpecW(path);
    ::SetCurrentDirectoryW(path);
}

static void SetDpiAwareness()
{
    // Prefer Per-Monitor-V2 if available, fall back to system DPI aware.
    HMODULE user32 = ::GetModuleHandleW(L"user32");
    if (user32) {
        using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto p = reinterpret_cast<Fn>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    ::SetProcessDPIAware();
}

// Minidump on crash for actionable bug reports.
static LONG WINAPI DumpUnhandled(EXCEPTION_POINTERS* ex)
{
    ::CreateDirectoryW(L"crash", nullptr);
    wchar_t dumpPath[MAX_PATH];
    SYSTEMTIME st; ::GetLocalTime(&st);
    swprintf(dumpPath, MAX_PATH, L"crash\\colony_%04u%02u%02u_%02u%02u%02u.dmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = ::CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = ::GetCurrentThreadId();
        mei.ExceptionPointers = ex;
        mei.ClientPointers = FALSE;

        ::MiniDumpWriteDump(::GetCurrentProcess(),
                            ::GetCurrentProcessId(),
                            hFile,
                            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
                            &mei, nullptr, nullptr);
        ::CloseHandle(hFile);
        Log(L"Wrote minidump: %s", dumpPath);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool SingleInstanceGuard()
{
    gMutex = ::CreateMutexW(nullptr, TRUE, L"{C3B9F8E7-9E5F-4D2C-9D52-A9D2CB62E8FD}");
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::MessageBoxW(nullptr, L"Colony is already running.", L"Colony", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

static bool LaunchGame()
{
    wchar_t exePath[MAX_PATH];
    if (!::GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;
    ::PathRemoveFileSpecW(exePath);
    ::PathAppendW(exePath, COLONY_GAME_EXE);

    if (!::PathFileExistsW(exePath)) {
        ::MessageBoxW(nullptr, L"Could not find " COLONY_GAME_EXE L" next to the launcher.",
                      L"Colony", MB_OK | MB_ICONERROR);
        Log(L"Missing game exe: %s", exePath);
        return false;
    }

    int argc = 0; LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring cmd = L"\""; cmd += exePath; cmd += L"\"";
    for (int i = 1; i < argc; ++i) { cmd += L" \""; cmd += argv[i]; cmd += L"\""; }
    ::LocalFree(argv);

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
#ifndef _DEBUG
    flags |= CREATE_NO_WINDOW; // keep release silent (no console)
#endif

    BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        DWORD err = ::GetLastError();
        Log(L"CreateProcessW failed (%lu) for: %s", err, cmd.c_str());
        ::MessageBoxW(nullptr, L"Failed to start the game. See logs\\launcher.log.",
                      L"Colony", MB_OK | MB_ICONERROR);
        return false;
    }

    ::CloseHandle(pi.hThread);
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0; ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    Log(L"Game exited with code %lu", (unsigned long)exitCode);
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    ::SetUnhandledExceptionFilter(DumpUnhandled);
    SetDpiAwareness();
    ForceWorkingDirectoryToExeDir();
    if (!SingleInstanceGuard()) return 0;

    const bool ok = LaunchGame();
    if (gMutex) { ::ReleaseMutex(gMutex); ::CloseHandle(gMutex); }
    return ok ? 0 : 1;
}
