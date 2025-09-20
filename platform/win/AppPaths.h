#pragma once
#include <filesystem>

namespace app::paths {
    std::filesystem::path exe_dir();            // folder of running .exe
    std::filesystem::path content_root();       // resolves .../res robustly
    std::filesystem::path logs_dir();           // %LOCALAPPDATA%/ColonyGame/logs
    bool set_cwd_to_exe();                      // SetCurrentDirectoryW(exe_dir)
    void ensure_utf8_console();                 // optional for debug console
}
