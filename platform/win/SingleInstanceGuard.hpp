#pragma once
#include <windows.h>

namespace colony::win {

class SingleInstanceGuard {
    HANDLE hMutex_{nullptr};
    bool primary_{false};
public:
    explicit SingleInstanceGuard(const wchar_t* name) {
        hMutex_ = ::CreateMutexW(nullptr, FALSE, name);
        DWORD gle = ::GetLastError();
        primary_ = (gle != ERROR_ALREADY_EXISTS && gle != ERROR_ACCESS_DENIED);
    }
    ~SingleInstanceGuard(){ if (hMutex_) ::CloseHandle(hMutex_); }
    explicit operator bool() const noexcept { return primary_; }
};

} // namespace colony::win
