// Minimal, self-contained crash handler bridge for Windows.
// Builds into the EXE only (not colony_core).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace {

LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* info)
{
    // Compute dump path: <exe>\crash\<AppName>_YYYYMMDD_HHMMSS.dmp
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::path dumpDir = exeDir / L"crash";
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    // Optional: read the app name we cached in InitCrashHandler
    const wchar_t* app = L"ColonyGame";
    if (auto h = GetModuleHandleW(nullptr))
    {
        wchar_t name[MAX_PATH]{};
        if (GetModuleFileNameW(h, name, MAX_PATH))
        {
            app = wcsrchr(name, L'\\') ? wcsrchr(name, L'\\') + 1 : name;
            // trim .exe
            size_t len = wcslen(app);
            if (len > 4 && _wcsicmp(app + len - 4, L".exe") == 0) {
                const_cast<wchar_t*>(app)[len - 4] = L'\0';
            }
        }
    }

    wchar_t fileName[256]{};
    swprintf_s(fileName, L"%s_%04u%02u%02u_%02u%02u%02u.dmp",
               app, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    auto dumpPath = dumpDir / fileName;

    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;

        // Reasonable default flags for game dumps
        MINIDUMP_TYPE type =
            (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
                            MiniDumpScanMemory |
                            MiniDumpWithThreadInfo |
                            MiniDumpWithUnloadedModules);

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, type, &mei, nullptr, nullptr);

        CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // anonymous namespace

namespace wincrash {

// Installs the process-wide crash handler. Call *once* during startup.
void InitCrashHandler(const wchar_t* /*appName*/)
{
    // Avoid Windows error UI that can hang automation
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(&TopLevelFilter);
}

} // namespace wincrash
