#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win/Bootstrap.h"
#include "core/Log.h"
#include <windows.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "user32.lib")

// Exported variables that nudge NV/AMD drivers to use discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}

namespace cg {

void SelectHighPerformanceGPU() {
    // Nothing to do at runtime; the exports above are enough.
    cg::Log::Info("High-performance GPU hint exported.");
}

void ConfigureDPI() {
    // Try Per-Monitor-V2 (Windows 10+), fall back gracefully.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setDpiCtx = reinterpret_cast<Fn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiCtx) {
            if (setDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                cg::Log::Info("DPI awareness: PerMonitorV2");
                FreeLibrary(user32);
                return;
            }
        }
        FreeLibrary(user32);
    }
    SetProcessDPIAware(); // Legacy fallback
    cg::Log::Info("DPI awareness: System (fallback)");
}

std::filesystem::path SetCurrentDirToExe() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path exePath = buf;
    std::filesystem::path exeDir  = exePath.remove_filename();

    SetDllDirectoryW(exeDir.c_str()); // Prefer local DLLs
    SetCurrentDirectoryW(exeDir.c_str());

    const std::string exeDirNarrow = exeDir.string();
    cg::Log::Info("Working dir set to: %s", exeDirNarrow.c_str());
    return exeDir;
}

std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir) {
    auto res = exeDir / "res";
    if (std::filesystem::exists(res)) {
        cg::Log::Info("res/ folder OK.");
        return res;
    }
    // Optional alternate (when running from build tree)
    auto alt = exeDir.parent_path() / "res";
    if (std::filesystem::exists(alt)) {
        cg::Log::Warn("res/ not next to EXE; using parent/res");
        return alt;
    }
    cg::Log::Error("res/ folder missing.");
    return {};
}

HANDLE CreateSingleInstanceMutex(const wchar_t* name) {
    HANDLE h = CreateMutexW(nullptr, FALSE, name);
    if (!h) return nullptr;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Colony-Game is already running.", L"Colony-Game",
                    MB_ICONINFORMATION | MB_OK);
        CloseHandle(h);
        return nullptr;
    }
    return h;
}

} // namespace cg
