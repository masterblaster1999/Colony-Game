#pragma once
#include <windows.h>
#include <string>

namespace winqol {
    class SingleInstance {
    public:
        explicit SingleInstance(const std::wstring& name) {
            h_ = CreateMutexW(nullptr, TRUE, name.c_str());
            already_ = (GetLastError() == ERROR_ALREADY_EXISTS);
        }
        ~SingleInstance() {
            if (h_) { ReleaseMutex(h_); CloseHandle(h_); }
        }
        bool alreadyRunning() const { return already_; }
    private:
        HANDLE h_{};
        bool already_{false};
    };
}
