#pragma once
#include <windows.h>
#include <string>

namespace cg::win {
    // Installs a top-level unhandled exception filter which writes a .dmp to dumpDir.
    // dumpDir will be created if it doesn't exist.
    // appTag appears in the filename, e.g. ColonyGame_1.0.0_2025-09-10_120102.dmp
    bool InstallCrashHandler(const std::wstring& dumpDir, const std::wstring& appTag);
}
