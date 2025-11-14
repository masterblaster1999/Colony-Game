// SingleInstance.cpp

// REMOVE this line:
// #define WIN32_LEAN_AND_MEAN

#include <Windows.h> // Already built with WIN32_LEAN_AND_MEAN via CMake

namespace {
    HANDLE gMutex = nullptr;
}

namespace app::single_instance {

bool acquire()
{
    gMutex = CreateMutexW(
        nullptr,
        TRUE,
        L"Local\\ColonyGame-1C06AE36-42F7-4E52-9DE6-37E8C0B52A43"
    );

    if (!gMutex)
        return true; // fail-open: don't block launch on OS error

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

} // namespace app::single_instance
