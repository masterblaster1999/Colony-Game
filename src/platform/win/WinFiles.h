#pragma once
#include <filesystem>

namespace platform::win {

// EXE directory (for assets shipped next to the binary)
std::filesystem::path GetExeDir();

// Ensure process working directory == EXE dir
void FixWorkingDirectory();

// %LOCALAPPDATA%\ColonyGame (created on demand)
std::filesystem::path GetSaveDir();

// %LOCALAPPDATA%\ColonyGame\logs (created on demand)
std::filesystem::path GetLogDir();

} // namespace platform::win
