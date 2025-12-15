// platform/win/LauncherSpawnWin.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace winlaunch
{
    struct SpawnResult
    {
        bool     succeeded        = false;
        uint32_t exit_code        = 0;
        uint32_t win32_error      = 0;
        std::wstring win32_error_text;
    };

    // Spawns the main game exe and waits for exit. Mirrors the child's exit code.
    //
    // - childArgs should be the launcher args intended for the child *excluding* argv[0].
    //   (Use BuildChildArguments() from LauncherCliWin.h.)
    SpawnResult SpawnAndWait(const std::filesystem::path& gameExe,
                             const std::filesystem::path& workingDir,
                             const std::wstring&          childArgs,
                             std::wofstream&              log);
}
