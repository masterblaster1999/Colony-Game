#ifdef _WIN32
#include "WinBootstrap.h"
#include "SingleInstance.h"
#include "CrashHandler.h"

#include <windows.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Hint GPU drivers to prefer discrete GPUs.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int  AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

void SetDpiAwareness() {
    // Try modern per‑monitor v2. If unavailable, fall back.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = reinterpret_cast<SetCtx>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    // Fallback for older Windows
    ::SetProcessDPIAware();
}

void SetWorkingDirectoryToExeFolder() {
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    fs::path exePath(path);
    fs::path dir = exePath.remove_filename();
    ::SetCurrentDirectoryW(dir.c_str());
}

} // namespace

namespace winboot {

void PrepareProcess(const std::wstring& appName,
                    const std::wstring& mutexName,
                    bool allowMultipleInstancesForDev) {
    SetWorkingDirectoryToExeFolder();
    SetDpiAwareness();

    if (!AcquireSingleInstance(mutexName, allowMultipleInstancesForDev)) {
        // Another instance exists; quit early.
        ExitProcess(0);
    }

    InstallCrashHandler(appName);
}

void CleanupProcess() {
    // Currently nothing to cleanup. Placeholder for future (e.g., releasing mutex explicitly).
}

} // namespace winboot
#endif
