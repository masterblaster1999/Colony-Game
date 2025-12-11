#include "platform/win/WinUtils.hpp"
#include "core/Log.h"

namespace cg::win {

std::filesystem::path GetExecutableDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path exePath = buf;
    return exePath.remove_filename();
}

std::filesystem::path SetCurrentDirToExe() {
    auto exeDir = GetExecutableDir();
    SetDllDirectoryW(exeDir.c_str());
    SetCurrentDirectoryW(exeDir.c_str());
    cg::Log::Info("Working dir set to: %s", exeDir.string().c_str());
    return exeDir;
}

std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir) {
    auto res = exeDir / "res";
    if (std::filesystem::exists(res)) return res;
    auto alt = exeDir.parent_path() / "res";
    if (std::filesystem::exists(alt)) {
        cg::Log::Warn("res/ not next to EXE; using parent/res");
        return alt;
    }
    cg::Log::Error("res/ folder missing.");
    return {};
}

void ConfigureDPI() {
    // Prefer manifest (recommended), fallback to API if needed.
    // Per‑Monitor‑V2 improves scaling/clarity on multi‑DPI setups.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx && setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            cg::Log::Info("DPI awareness: PerMonitorV2");
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }
    SetProcessDPIAware(); // legacy fallback
    cg::Log::Info("DPI awareness: System (fallback)");
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
} // namespace cg::win
