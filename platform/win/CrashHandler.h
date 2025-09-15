#pragma once
#include <filesystem>

namespace cg {
    // Creates crashdumps in `dumpDir` on unhandled exceptions.
    void InstallCrashHandler(const std::filesystem::path& dumpDir);
}
