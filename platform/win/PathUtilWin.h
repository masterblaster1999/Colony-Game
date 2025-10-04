#pragma once
#include <filesystem>
#include <cstddef>

namespace winpath {
    std::filesystem::path exe_path();              // Full path to the running .exe
    std::filesystem::path exe_dir();               // Directory of the .exe
    void ensure_cwd_exe_dir();                     // Sets CWD to exe_dir (fixes asset-relative paths)
    std::filesystem::path resource_dir();          // exe_dir()/res
    std::filesystem::path writable_data_dir();     // %LOCALAPPDATA%/ColonyGame (created if missing)

    // Added declarations (previously defined in the .cpp but missing here)
    std::filesystem::path saved_games_dir(const wchar_t* app_name);
    bool atomic_write_file(const std::filesystem::path& final_path,
                           const void* data, size_t size_bytes);
}
