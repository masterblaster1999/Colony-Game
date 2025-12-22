// engine/win/CrashHandler.cpp
#include <filesystem>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>

#include <DbgHelp.h>
#include <ShlObj.h>

#include "CrashHandler.h"

#pragma comment(lib, "dbghelp.lib")

namespace
{
std::filesystem::path GetCrashDumpDirectory() noexcept
{
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
    {
        return std::filesystem::path(appData) / L"ColonyGame" / L"Crashes";
    }

    // Fallback: current directory.
    return std::filesystem::path(L".") / L"Crashes";
}

std::wstring MakeDumpFilename() noexcept
{
    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    wchar_t name[128]{};
    (void)::swprintf_s(name,
                       L"%04u%02u%02u_%02u%02u%02u.dmp",
                       st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
    return name;
}

LONG WINAPI ColonyUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) noexcept
{
    const std::filesystem::path dir = GetCrashDumpDirectory();

    // Avoid exceptions in an exception filter.
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path dumpPath = dir / MakeDumpFilename();

    HANDLE hFile = ::CreateFileW(dumpPath.c_str(),
                                 GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 nullptr,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId          = ::GetCurrentThreadId();
        info.ExceptionPointers = ep;
        info.ClientPointers    = FALSE;

        // FIX: argument #4 must be MINIDUMP_TYPE (bitmask enum), not int.
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

        (void)::MiniDumpWriteDump(::GetCurrentProcess(),
                                  ::GetCurrentProcessId(),
                                  hFile,
                                  dumpType,
                                  ep ? &info : nullptr,
                                  nullptr,
                                  nullptr);

        ::CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

namespace winplat
{
void InstallCrashHandler(const wchar_t* /*gameName*/)
{
    // Avoid OS popups; let our handler create a dump.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    (void)::SetUnhandledExceptionFilter(&ColonyUnhandledExceptionFilter);
}
} // namespace winplat
