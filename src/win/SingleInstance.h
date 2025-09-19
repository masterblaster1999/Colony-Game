#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

class SingleInstance {
public:
    explicit SingleInstance(const std::wstring& mutexName)
    : hMutex_(CreateMutexW(nullptr, FALSE, mutexName.c_str())),
      already_(GetLastError() == ERROR_ALREADY_EXISTS) {}

    ~SingleInstance() { if (hMutex_) CloseHandle(hMutex_); }

    bool already_running() const { return already_; }

private:
    HANDLE hMutex_{};
    bool   already_{false};
};
