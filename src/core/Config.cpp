#include "Config.h"
#include "Log.h"
#include <fstream>
#include <sstream>

namespace core {

static std::filesystem::path Path(const std::filesystem::path& dir) {
    return dir / "config.ini";
}

// Tiny INI-style parser: key=value lines
bool LoadConfig(Config& cfg, const std::filesystem::path& saveDir) {
    std::ifstream f(Path(saveDir));
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos+1);
        if (k == "windowWidth")  cfg.windowWidth  = std::stoi(v);
        else if (k == "windowHeight") cfg.windowHeight = std::stoi(v);
        else if (k == "vsync")   cfg.vsync = (v == "1" || v == "true" || v == "True");
    }
    return true;
}

bool SaveConfig(const Config& cfg, const std::filesystem::path& saveDir) {
    std::ofstream f(Path(saveDir), std::ios::trunc);
    if (!f) return false;
    f << "windowWidth="  << cfg.windowWidth  << "\n";
    f << "windowHeight=" << cfg.windowHeight << "\n";
    f << "vsync="        << (cfg.vsync ? 1 : 0) << "\n";
    return true;
}

} // namespace core
