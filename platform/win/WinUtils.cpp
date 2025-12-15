#include "platform/win/WinUtils.hpp"
#include "core/Log.h"

namespace cg::win {

std::filesystem::path SetCurrentDirToExe()
{
    auto exeDir = GetExecutableDir();
    SetDllDirectoryW(exeDir.c_str());
    SetCurrentDirectoryW(exeDir.c_str());

    core::LogMessage(core::LogLevel::Info, "Working dir set to: %s", exeDir.string().c_str());
    return exeDir;
}

std::filesystem::path EnsureResPresent(const std::filesystem::path& exeDir)
{
    auto res = exeDir / "res";
    if (std::filesystem::exists(res)) return res;

    auto alt = exeDir.parent_path() / "res";
    if (std::filesystem::exists(alt)) {
        core::LogMessage(core::LogLevel::Warn, "res/ not next to EXE; using parent/res");
        return alt;
    }

    core::LogMessage(core::LogLevel::Error, "res/ folder missing.");
    return {};
}

void ConfigureDPI()
{
    // ...
    core::LogMessage(core::LogLevel::Info, "DPI awareness: PerMonitorV2");
    // ...
    core::LogMessage(core::LogLevel::Info, "DPI awareness: System (fallback)");
}

} // namespace cg::win
