#pragma once
#ifdef _WIN32
#include <string>

namespace winboot {

// Call once at the very start of main()/wWinMain().
// mutexName example: L"Global\\ColonyGame_SingleInstance"
void PrepareProcess(const std::wstring& appName,
                    const std::wstring& mutexName,
                    bool allowMultipleInstancesForDev = false);

// Optional: call on shutdown (currently a no‑op, reserved for future use).
void CleanupProcess();

} // namespace winboot
#endif
