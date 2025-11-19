// CrashDumpWin.h
#pragma once

#include <windows.h>

class CrashDumpGuard {
public:
    explicit CrashDumpGuard(const wchar_t* appName) noexcept;
    ~CrashDumpGuard();

    CrashDumpGuard(const CrashDumpGuard&) = delete;
    CrashDumpGuard& operator=(const CrashDumpGuard&) = delete;

private:
    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* info) noexcept;

    const wchar_t* _appName{};
    LPTOP_LEVEL_EXCEPTION_FILTER _prevFilter = nullptr;
};
