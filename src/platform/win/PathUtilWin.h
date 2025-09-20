#pragma once
#include <filesystem>

namespace winpath {
    std::filesystem::path exe_path();              // Full path to the running .exe
    std::filesystem::path exe_dir();               // Directory of the .exe
    void ensure_cwd_exe_dir();                     // Sets CWD to exe_dir (fixes asset-relative paths)
    std::filesystem::path resource_dir();          // exe_dir()/res
    std::filesystem::path writable_data_dir();     // %LOCALAPPDATA%/ColonyGame (created if missing)
}
