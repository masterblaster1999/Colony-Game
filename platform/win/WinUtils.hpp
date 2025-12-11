#pragma once
#include <filesystem>
#include <windows.h>

namespace cg::win {
    std::filesystem::path GetExecutableDir();
    std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir);
    std::filesystem::path SetCurrentDirToExe();
    void  ConfigureDPI(); // Per‑Monitor‑V2 with manifest or API fallback
    HANDLE CreateSingleInstanceMutex(const wchar_t* name);
}
