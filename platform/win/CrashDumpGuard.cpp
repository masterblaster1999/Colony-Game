// platform/win/CrashDumpGuard.cpp

#ifdef _WIN32
#include "CrashDumpGuard.h"

#include <dbghelp.h>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

CrashDumpGuard* CrashDumpGuard::s_instance = nullptr;

CrashDumpGuard::CrashDumpGuard(std::wstring dumpDirectory,
                               std::wstring appName)
    : m_dumpDirectory(std::move(dumpDirectory)),
      m_appName(std::move(appName)),
      m_prevFilter(nullptr)
{
    s_instance  = this;
    m_prevFilter = ::SetUnhandledExceptionFilter(&CrashDumpGuard::UnhandledExceptionFilter);
}

CrashDumpGuard::~CrashDumpGuard()
{
    ::SetUnhandledExceptionFilter(m_prevFilter);
    s_instance = nullptr;
}

LONG WINAPI CrashDumpGuard::UnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    if (!s_instance)
        return EXCEPTION_EXECUTE_HANDLER;

    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    wchar_t fileName[256];
    swprintf_s(fileName, L"%s_%04d%02d%02d_%02d%02d%02d.dmp",
               s_instance->m_appName.c_str(),
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    std::filesystem::create_directories(s_instance->m_dumpDirectory);
    std::filesystem::path dumpPath = std::filesystem::path(s_instance->m_dumpDirectory) / fileName;

    HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return EXCEPTION_EXECUTE_HANDLER;

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId          = ::GetCurrentThreadId();
    info.ExceptionPointers = pExceptionInfo;
    info.ClientPointers    = FALSE;

    ::MiniDumpWriteDump(::GetCurrentProcess(),
                        ::GetCurrentProcessId(),
                        hFile,
                        MiniDumpWithIndirectlyReferencedMemory,
                        &info,
                        nullptr,
                        nullptr);

    ::CloseHandle(hFile);

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // _WIN32
