// platform/win/Paths.h
#pragma once
#include <filesystem>
#include <string_view>

namespace Colony::Win::Paths
{
    // Returns (and creates) %USERPROFILE%\Saved Games\<AppName>\
    std::filesystem::path SavedGames(std::wstring_view appName);

    // Returns (and creates) %LOCALAPPDATA%\<AppName>\ (good for caches, logs)
    std::filesystem::path LocalAppData(std::wstring_view appName);

    // Returns (and creates) %APPDATA%\<AppName>\ (roaming profile; good for config)
    std::filesystem::path RoamingAppData(std::wstring_view appName);

    // Convenience subfolders (created if needed)
    std::filesystem::path Logs(std::wstring_view appName);
    std::filesystem::path Config(std::wstring_view appName);
}
