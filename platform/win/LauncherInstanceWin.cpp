#include "platform/win/LauncherInstanceWin.h"

SingleInstanceGuard::SingleInstanceGuard()
    : h_(nullptr)
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (h_)
        CloseHandle(h_);
}

bool SingleInstanceGuard::acquire(const wchar_t* name)
{
    h_ = CreateMutexW(nullptr, FALSE, name);
    return h_ && GetLastError() != ERROR_ALREADY_EXISTS;
}
