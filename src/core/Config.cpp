#include "Config.h"
#include "Log.h"
#include "util/TextEncoding.h"

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

static bool EqualsI(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }

    return true;
}

static bool ParseBool(std::string_view sv, bool& out) noexcept
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);

    // Common INI boolean tokens (case-insensitive):
    //   true  values:  1, true, yes, on
    //   false values:  0, false, no, off
    if (sv == "1") { out = true; return true; }
    if (sv == "0") { out = false; return true; }

    if (EqualsI(sv, "true") || EqualsI(sv, "yes") || EqualsI(sv, "on"))
    {
        out = true;
        return true;
    }

    if (EqualsI(sv, "false") || EqualsI(sv, "no") || EqualsI(sv, "off"))
    {
        out = false;
        return true;
    }

    return false;
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


    // Config is user-editable. Normalize to UTF-8 so files saved by Windows editors
    // (UTF-8 BOM / UTF-16 BOM) remain parseable.
    if (!colony::util::NormalizeTextToUtf8(text))
    {
        LOG_WARN("LoadConfig: NormalizeTextToUtf8 failed for %s", path.string().c_str());
        return false;
    }

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
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

        // Strip trailing inline comments (simple INI-style).
        // This makes config.ini friendlier to hand-edit, e.g.:
        //   windowWidth=1280  # pixels
        //   windowHeight=720  ; pixels
        //   vsync=true        // enable vsync
        {
            const std::size_t hashPos  = v.find('#');
            const std::size_t semiPos  = v.find(';');
            const std::size_t slashPos = v.find("//");

            std::size_t cut = std::string::npos;
            auto consider = [&](std::size_t p)
            {
                if (p == std::string::npos) return;
                if (cut == std::string::npos || p < cut) cut = p;
            };

            consider(hashPos);
            consider(semiPos);
            consider(slashPos);

            if (cut != std::string::npos)
            {
                v.erase(cut);
                TrimInPlace(v);
            }
        }

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
            bool parsed = cfg.vsync;
            if (ParseBool(v, parsed))
                cfg.vsync = parsed;
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
