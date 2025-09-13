#ifdef _WIN32
#include "SingleInstance.h"
#include <windows.h>

namespace winboot {

bool AcquireSingleInstance(const std::wstring& mutexName, bool allowMultipleInstancesForDev) {
    if (allowMultipleInstancesForDev) return true;

    HANDLE hMutex = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!hMutex) return true; // fail open rather than hard‑fail

    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"The game is already running.",
                    L"Colony Game", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return false;
    }
    return true;
}

} // namespace winboot
#endif
