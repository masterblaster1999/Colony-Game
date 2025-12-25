#include "UserSettings.h"

#include "platform/win/PathUtilWin.h"
#include "platform/win/WinFiles.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace colony::appwin {

namespace {
    constexpr std::uint32_t kMinWindowDim = 640;
    constexpr std::uint32_t kMaxWindowDim = 7680;

    constexpr int kMinMaxFps = 30;
    constexpr int kMaxMaxFps = 1000;


    constexpr int kMinFrameLatency = 1;
    constexpr int kMaxFrameLatency = 16;

    std::uint32_t ClampDim(std::uint32_t v) noexcept
    {
        if (v < kMinWindowDim) return kMinWindowDim;
        if (v > kMaxWindowDim) return kMaxWindowDim;
        return v;
    }

    int ClampMaxFps(int v) noexcept
    {
        if (v == 0) return 0;
        if (v < kMinMaxFps) return kMinMaxFps;
        if (v > kMaxMaxFps) return kMaxMaxFps;
        return v;
    }


    int ClampFrameLatency(int v) noexcept
    {
        if (v < kMinFrameLatency) return kMinFrameLatency;
        if (v > kMaxFrameLatency) return kMaxFrameLatency;
        return v;
    }

    bool ReadFileToString(const std::filesystem::path& p, std::string& out) noexcept
    {
        out.clear();
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        const std::streamoff sz = f.tellg();
        if (sz <= 0) return false;
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(sz));
        f.read(out.data(), static_cast<std::streamsize>(sz));
        return true;
    }
}

std::filesystem::path UserSettingsPath()
{
    // Reuse the same root as other persisted user data.
    // (WinFiles already uses KnownFolders first, env fallback second.)
    auto root = platform::win::GetSaveDir();
    return root / "settings.json";
}

bool LoadUserSettings(UserSettings& out) noexcept
{
    std::string text;
    const auto path = UserSettingsPath();
    if (!ReadFileToString(path, text))
        return false;

    // Allow // comments (same as input_bindings.json), and avoid exceptions.
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false, /*ignore_comments*/ true);
    if (j.is_discarded() || !j.is_object())
        return false;

    UserSettings tmp = out;

    if (const auto it = j.find("window"); it != j.end() && it->is_object())
    {
        if (auto w = it->find("width"); w != it->end() && w->is_number_integer())
            tmp.windowWidth = ClampDim(static_cast<std::uint32_t>(w->get<std::int64_t>()));
        if (auto h = it->find("height"); h != it->end() && h->is_number_integer())
            tmp.windowHeight = ClampDim(static_cast<std::uint32_t>(h->get<std::int64_t>()));
    }

    if (const auto it = j.find("graphics"); it != j.end() && it->is_object())
    {
        if (auto v = it->find("vsync"); v != it->end() && v->is_boolean())
            tmp.vsync = v->get<bool>();
        if (auto f = it->find("fullscreen"); f != it->end() && f->is_boolean())
            tmp.fullscreen = f->get<bool>();
        if (auto m = it->find("maxFpsWhenVsyncOff"); m != it->end() && m->is_number_integer())
            tmp.maxFpsWhenVsyncOff = ClampMaxFps(m->get<int>());
        if (auto l = it->find("maxFrameLatency"); l != it->end() && l->is_number_integer())
            tmp.maxFrameLatency = ClampFrameLatency(l->get<int>());
    }

    if (const auto it = j.find("runtime"); it != j.end() && it->is_object())
    {
        if (auto p = it->find("pauseWhenUnfocused"); p != it->end() && p->is_boolean())
            tmp.pauseWhenUnfocused = p->get<bool>();
        if (auto m = it->find("maxFpsWhenUnfocused"); m != it->end() && m->is_number_integer())
            tmp.maxFpsWhenUnfocused = ClampMaxFps(m->get<int>());
    }

    out = tmp;
    return true;
}

bool SaveUserSettings(const UserSettings& settings) noexcept
{
    // Ensure base directories exist.
    winpath::ensure_dirs();
    std::error_code ec;
    std::filesystem::create_directories(UserSettingsPath().parent_path(), ec);

    nlohmann::json j;
    j["version"] = 3;
    j["window"] = {
        {"width", settings.windowWidth},
        {"height", settings.windowHeight},
    };
    j["graphics"] = {
        {"vsync", settings.vsync},
        {"fullscreen", settings.fullscreen},
        {"maxFpsWhenVsyncOff", settings.maxFpsWhenVsyncOff},
        {"maxFrameLatency", settings.maxFrameLatency},
    };

    j["runtime"] = {
        {"pauseWhenUnfocused", settings.pauseWhenUnfocused},
        {"maxFpsWhenUnfocused", settings.maxFpsWhenUnfocused},
    };

    std::string payload = j.dump(4);
    payload.push_back('\n');

    return winpath::atomic_write_file(UserSettingsPath(), payload);
}

} // namespace colony::appwin
