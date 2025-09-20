// SingleInstance.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
    HANDLE gMutex = nullptr;
}

namespace app::single_instance {
    bool acquire() {
        gMutex = CreateMutexW(nullptr, TRUE, L"Local\\ColonyGame-1C06AE36-42F7-4E52-9DE6-37E8C0B52A43");
        if (!gMutex) return true; // fail-open
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            return false;
        }
        return true;
    }
}
