#include "Config.h"
#include "Log.h"

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include "platform/win/PathUtilWin.h"
#endif

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace core {

static std::filesystem::path Path(const std::filesystem::path& dir) {
    return dir / "config.ini";
}

static inline void TrimInPlace(std::string& s)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

    while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

static bool ParseInt(std::string_view sv, int& out) noexcept
{
    // trim leading/trailing whitespace
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);

    int v = 0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc{} || ptr != end)
        return false;

    out = v;
    return true;
}

static bool ParseBool(std::string_view sv) noexcept
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);

    std::string v(sv);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return (v == "1" || v == "true" || v == "yes" || v == "on");
}

// Tiny INI-style parser: key=value lines
bool LoadConfig(Config& cfg, const std::filesystem::path& saveDir)
{
    const auto path = Path(saveDir);

    std::string text;
#if defined(_WIN32)
    // Config is user-editable and can be briefly locked by editors/scanners.
    // Use retry/backoff reads to avoid spurious failures during hot-reload / startup.
    std::error_code ec;
    if (!winpath::read_file_to_string_with_retry(path, text, &ec,
                                                 /*max_bytes=*/1024u * 1024u,
                                                 /*max_attempts=*/32))
    {
        // Missing config is normal on first run; don't spam logs.
        if (ec.value() != ERROR_FILE_NOT_FOUND && ec.value() != ERROR_PATH_NOT_FOUND)
            LOG_WARN("LoadConfig: failed to read %s (%d: %s)", path.string().c_str(), ec.value(), ec.message().c_str());
        return false;
    }
#else
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    text = oss.str();
#endif

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        // Strip UTF-8 BOM on first line if present.
        if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF)
        {
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }
        }

        // Comments / empty
        std::string tmp = line;
        TrimInPlace(tmp);
        if (tmp.empty()) continue;
        if (tmp[0] == '#' || tmp[0] == ';') continue;

        const auto pos = tmp.find('=');
        if (pos == std::string::npos) continue;

        std::string k = tmp.substr(0, pos);
        std::string v = tmp.substr(pos + 1);
        TrimInPlace(k);
        TrimInPlace(v);
        if (k.empty()) continue;

        if (k == "windowWidth")
        {
            int parsed = cfg.windowWidth;
            if (ParseInt(v, parsed))
                cfg.windowWidth = parsed;
        }
        else if (k == "windowHeight")
        {
            int parsed = cfg.windowHeight;
            if (ParseInt(v, parsed))
                cfg.windowHeight = parsed;
        }
        else if (k == "vsync")
        {
            cfg.vsync = ParseBool(v);
        }
    }

    return true;
}

bool SaveConfig(const Config& cfg, const std::filesystem::path& saveDir)
{
    std::error_code ec;
    std::filesystem::create_directories(saveDir, ec);
    if (ec)
    {
        LOG_ERROR("SaveConfig: create_directories failed for %s (%d: %s)",
                  saveDir.string().c_str(), ec.value(), ec.message().c_str());
        return false;
    }

    std::ostringstream oss;
    oss << "windowWidth="  << cfg.windowWidth  << "\n";
    oss << "windowHeight=" << cfg.windowHeight << "\n";
    oss << "vsync="        << (cfg.vsync ? 1 : 0) << "\n";
    const std::string text = oss.str();

    const auto path = Path(saveDir);

#if defined(_WIN32)
    std::error_code wec;
    if (!winpath::atomic_write_file(path, text, &wec))
    {
        LOG_ERROR("SaveConfig: atomic_write_file failed for %s (%d: %s)",
                  path.string().c_str(), wec.value(), wec.message().c_str());
        return false;
    }
    return true;
#else
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
#endif
}

} // namespace core
