#include "Log.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <windows.h>
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;
static std::shared_ptr<spdlog::logger> g_logger;

static fs::path logs_dir() {
    PWSTR p = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p))) {
        base = p; CoTaskMemFree(p);
    }
    fs::path dir = base / L"ColonyGame" / L"logs";
    std::error_code ec; fs::create_directories(dir, ec);
    return dir;
}

void logsys::init_windows_logs() {
    auto file = (logs_dir() / L"colony.log").string();
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file, 1 << 20, 4); // 1MB * 4
    g_logger = std::make_shared<spdlog::logger>("colony", sink);
    spdlog::set_default_logger(g_logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l] %v");
    spdlog::info("Logging started");
}
std::shared_ptr<spdlog::logger> logsys::get() { return g_logger; }
