#pragma once
#include <filesystem>
#include <string>

namespace cg::winpaths {
    // %LOCALAPPDATA% (FOLDERID_LocalAppData) or a safe fallback
    std::filesystem::path local_appdata();

    // %LOCALAPPDATA%\ColonyGame
    std::filesystem::path save_root();

    // %LOCALAPPDATA%\ColonyGame\Saves\<profile>  (ensures the directory exists)
    std::filesystem::path ensure_profile_dir(const std::string& profile);
}
