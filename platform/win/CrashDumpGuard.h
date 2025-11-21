// platform/win/CrashDumpGuard.h
#pragma once

#ifdef _WIN32

#include <string>
#include <windows.h>

class CrashDumpGuard
{
public:
    explicit CrashDumpGuard(std::wstring dumpDirectory = L".",
                            std::wstring appName       = L"ColonyGame");
    ~CrashDumpGuard();

private:
    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo);

    static CrashDumpGuard* s_instance;
    std::wstring m_dumpDirectory;
    std::wstring m_appName;
    LPTOP_LEVEL_EXCEPTION_FILTER m_prevFilter;
};

#endif // _WIN32
