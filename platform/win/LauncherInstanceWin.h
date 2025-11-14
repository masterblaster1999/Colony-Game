#pragma once

#include <windows.h>

// Simple RAII mutex guard to enforce single instance of the launcher
class SingleInstanceGuard
{
public:
    SingleInstanceGuard();
    ~SingleInstanceGuard();

    // Returns true if this process acquired the mutex; false if another
    // instance already holds it.
    bool acquire(const wchar_t* name);

private:
    HANDLE h_;
};
