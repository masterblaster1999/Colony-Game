// CrashHandler.h
#pragma once
#include <filesystem>
namespace app::crash {
    void install_minidump_handler(const std::filesystem::path& dumpDir);
}
