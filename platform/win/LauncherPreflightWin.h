// platform/win/LauncherPreflightWin.h
#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace winlaunch
{
    // Check that required content/shader folders exist under `root`.
    // Writes per-folder info into the log stream.
    bool CheckEssentialFiles(const std::filesystem::path& root,
                             std::wstring&               errorOut,
                             std::wofstream&             log);

    // Optional environment override for the game EXE path.
    //   COLONY_GAME_EXE="C:\foo\bar\MyGame.exe"  (absolute)
    //   COLONY_GAME_EXE="ColonyGame.exe"         (relative to launcher dir)
    std::optional<std::filesystem::path> EnvExeOverride();

    // Builds candidate list and returns the first existing executable, or empty path.
    // If outCandidates is non-null, it will be filled with the candidate paths in the exact order tried.
    std::filesystem::path FindGameExecutable(const std::filesystem::path&        exeDir,
                                             const std::wstring&                cliExeOverride,
                                             std::wofstream&                    log,
                                             std::vector<std::filesystem::path>* outCandidates);

    // Convenience helper to build a user-facing message listing attempted candidates.
    std::wstring BuildExeNotFoundMessage(const std::vector<std::filesystem::path>& candidates);
}
