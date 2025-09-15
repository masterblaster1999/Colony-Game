#pragma once
#include <filesystem>

namespace win {
    std::filesystem::path ExecutableDir();
    void SetWorkingDirToExecutableDir(); // call once at startup
}
