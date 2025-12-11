#pragma once
#include <filesystem>
namespace cg::win {
    std::filesystem::path GetExecutableDir();
    std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir);
    void SetCurrentDirToExe();
    void ConfigureDPI(); // Per‑Monitor V2, see §3.2
    void* CreateSingleInstanceMutex(const wchar_t* mutexName);
}
