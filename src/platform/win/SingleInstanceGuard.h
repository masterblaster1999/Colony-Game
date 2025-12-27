#pragma once

#include "platform/win/WinCommon.h"

#include <stdexcept>

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const wchar_t* mutexName)
        : handle_(CreateMutexW(nullptr, FALSE, mutexName))
    {
        if (!handle_)
            throw std::runtime_error("CreateMutexW failed");
        primary_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }

    ~SingleInstanceGuard()
    {
        if (handle_)
            CloseHandle(handle_);
    }

    bool IsPrimary() const { return primary_; }

private:
    HANDLE handle_ = nullptr;
    bool primary_ = false;
};
