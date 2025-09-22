#pragma once
#include <filesystem>
#include <string>

namespace winpath {

    // ------------------------------------------------------------------------------------
    // Application identity (used to build standard Windows paths)
    // ------------------------------------------------------------------------------------

    // Company/organization name used under %LOCALAPPDATA%\{Company}\...
    // Defaults should be defined in the .cpp; keep functions (not macros) so callers
    // may query dynamically or we can later allow overriding via a setter.
    std::wstring app_company();   // e.g., L"ColonyGame"

    // Product/game name used under Known Folders such as "Saved Games\{Product}"
    std::wstring app_product();   // e.g., L"Colony Game"

    // ------------------------------------------------------------------------------------
    // Executable & working directory
    // ------------------------------------------------------------------------------------

    // Full path to the running .exe
    std::filesystem::path exe_path();

    // Directory of the .exe
    std::filesystem::path exe_dir();

    // Sets CWD to exe_dir (stabilizes relative asset paths regardless of launch context)
    void ensure_cwd_exe_dir();

    // ------------------------------------------------------------------------------------
    // Content & data roots
    // ------------------------------------------------------------------------------------

    // Read-only game assets: <exe_dir>/res
    std::filesystem::path resource_dir();

    // Backwards-compatible writable root:
    // %LOCALAPPDATA%\{Company}  (created if missing)
    // Keep for existing callers; prefer config_dir()/logs_dir() for clarity.
    std::filesystem::path writable_data_dir();

    // Preferred config root:
    // %LOCALAPPDATA%\{Company}
    std::filesystem::path config_dir();

    // Logs folder:
    // %LOCALAPPDATA%\{Company}\logs
    std::filesystem::path logs_dir();

    // Crash dumps folder:
    // %LOCALAPPDATA%\{Company}\crashdumps
    std::filesystem::path crashdump_dir();

    // Saved games folder:
    // Known Folder "Saved Games\{Product}" (FOLDERID_SavedGames). If unavailable, falls back to
    // "Documents\Saved Games\{Product}".
    std::filesystem::path saved_games_dir();

    // Ensure all standard directories exist (config/logs/crashdumps/saved games).
    void ensure_dirs();

} // namespace winpath
