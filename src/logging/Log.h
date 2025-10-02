#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace logsys {
    void init_windows_logs();            // rotates in %LOCALAPPDATA%\ColonyGame\logs
    std::shared_ptr<spdlog::logger> get();  // "colony"
}
