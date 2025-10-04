#pragma once
#include <filesystem>

namespace core {

// Installs an unhandled exception filter that writes a minidump under dumpDir.
void InstallCrashHandler(const std::filesystem::path& dumpDir);

} // namespace core
