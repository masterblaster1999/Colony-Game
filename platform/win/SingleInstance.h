#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class SingleInstanceGuard {
    HANDLE m_mutex = nullptr;
public:
    explicit SingleInstanceGuard(const wchar_t* name) {
        m_mutex = CreateMutexW(nullptr, FALSE, name);
    }
    bool already_running() const {
        return (m_mutex && GetLastError() == ERROR_ALREADY_EXISTS);
    }
    ~SingleInstanceGuard() {
        if (m_mutex) CloseHandle(m_mutex);
    }
};
