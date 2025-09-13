#pragma once
#include <string>

namespace winqol {
    // %LOCALAPPDATA%\ColonyGame   (created if missing)
    std::wstring AppDataRoot(const std::wstring& appName);

    // %LOCALAPPDATA%\ColonyGame\logs   (created if missing)
    std::wstring LogsDir(const std::wstring& appName);

    // %LOCALAPPDATA%\ColonyGame\dumps  (created if missing)
    std::wstring DumpsDir(const std::wstring& appName);

    // Folder where the current EXE resides (no trailing slash).
    std::wstring ExeDir();

    // Utility: ensure a directory exists (creates intermediate dirs).
    void EnsureDir(const std::wstring& path);
}
