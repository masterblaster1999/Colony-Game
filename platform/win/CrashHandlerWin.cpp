// platform/win/CrashHandlerWin.cpp
// Minimal Windows crash-dump writer using DbgHelp::MiniDumpWriteDump.

#include <windows.h>
#include <dbghelp.h>
#include <knownfolders.h>
#include <shlobj.h>         // SHGetKnownFolderPath
#include <filesystem>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace {

// Cached app name used for the dump directory.
// Stored as a std::wstring so callers can pass stack pointers safely.
static std::wstring g_app_name = L"Colony Game";

static const wchar_t* AppName() noexcept
{
    return g_app_name.empty() ? L"Colony Game" : g_app_name.c_str();
}

std::filesystem::path SavedGamesDir(const wchar_t* appName)
{
    PWSTR path = nullptr;
    std::filesystem::path out;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &path)))
    {
        out = std::filesystem::path(path) / appName / L"Crashes";
        CoTaskMemFree(path);
    }

    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    return out;
}

LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* ep)
{
    const auto dir = SavedGamesDir(AppName());

    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t fileName[128]{};
    swprintf_s(fileName, L"%04u%02u%02u-%02u%02u%02u.dmp",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    const auto dumpPath = dir / fileName;

    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        // Include extra context useful for postmortem analysis without huge dumps.
        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), hFile,
            (MINIDUMP_TYPE)(
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpWithDataSegs |
                MiniDumpWithThreadInfo),
            &mei, nullptr, nullptr);

        CloseHandle(hFile);
    }

    // Let the default unhandled-exception machinery proceed after we wrote the dump.
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

namespace wincrash
{
    void InitCrashHandler(const wchar_t* appName)
    {
        if (appName && *appName)
            g_app_name = appName;

        // First handler in chain to maximize chances of running.
        AddVectoredExceptionHandler(/*First=*/1, VectoredHandler);
    }
} // namespace wincrash
