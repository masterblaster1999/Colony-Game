// platform/win/LauncherInstanceWin.cpp

#include "platform/win/WinCommon.h"
#include "platform/win/LauncherInstanceWin.h"

SingleInstanceGuard::SingleInstanceGuard()
    : h_(nullptr)
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (h_) {
        CloseHandle(h_);
        h_ = nullptr;
    }
}

bool SingleInstanceGuard::acquire(const wchar_t* name)
{
    // Create a named mutex. If it already exists and is owned by another
    // process, GetLastError() will return ERROR_ALREADY_EXISTS.
    h_ = CreateMutexW(nullptr, FALSE, name);
    return h_ && GetLastError() != ERROR_ALREADY_EXISTS;
}
