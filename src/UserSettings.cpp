#include "UserSettings.h"

#include "platform/win/PathUtilWin.h"
#include "platform/win/WinFiles.h"
#include "util/TextEncoding.h"

#include <string>

#include <nlohmann/json.hpp>

namespace colony::appwin {

namespace {
    // FPS caps:
    //   - 0 means uncapped
    //   - otherwise allow very low caps (e.g. 5/10 FPS when unfocused)
    //     because the window layer supports them and they are useful for
    //     background power saving.
    constexpr int kMinMaxFps = 1;
    constexpr int kMaxMaxFps = 1000;

    constexpr int kMinFrameLatency = 1;
    constexpr int kMaxFrameLatency = 16;

    // Settings JSON schema version.
    //
    // NOTE: SaveUserSettings always writes the latest schema version. LoadUserSettings
    // performs best-effort migration from older layouts so user preferences survive refactors.
    constexpr int kUserSettingsSchemaVersion = 5;

    std::uint32_t ClampWindowWidth(std::uint32_t v) noexcept
    {
        if (v < kMinWindowClientWidth) return kMinWindowClientWidth;
        if (v > kMaxWindowClientWidth) return kMaxWindowClientWidth;
        return v;
    }

    std::uint32_t ClampWindowHeight(std::uint32_t v) noexcept
    {
        if (v < kMinWindowClientHeight) return kMinWindowClientHeight;
        if (v > kMaxWindowClientHeight) return kMaxWindowClientHeight;
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

    SwapchainScalingMode ParseScalingMode(const nlohmann::json& v, SwapchainScalingMode fallback) noexcept
    {
        if (v.is_string()) {
            const std::string s = v.get<std::string>();
            if (s == "none" || s == "None") return SwapchainScalingMode::None;
            if (s == "stretch" || s == "Stretch") return SwapchainScalingMode::Stretch;
            if (s == "aspect" || s == "aspect_ratio" || s == "Aspect") return SwapchainScalingMode::Aspect;
            return fallback;
        }

        // Back-compat: accept integers.
        if (v.is_number_integer()) {
            const int i = v.get<int>();
            switch (i) {
            case 0: return SwapchainScalingMode::None;
            case 1: return SwapchainScalingMode::Stretch;
            case 2: return SwapchainScalingMode::Aspect;
            default: return fallback;
            }
        }
        return fallback;
    }

    const char* ScalingModeToString(SwapchainScalingMode m) noexcept
    {
        switch (m) {
        case SwapchainScalingMode::None: return "none";
        case SwapchainScalingMode::Stretch: return "stretch";
        case SwapchainScalingMode::Aspect: return "aspect";
        default: return "none";
        }
    }

    bool ReadFileToString(const std::filesystem::path& p, std::string& out) noexcept
    {
        out.clear();

        // Settings can be briefly locked by background scanners (Defender), Explorer preview handlers,
        // or editors saving via temporary file swaps. Use our Win32 retry/backoff helper.
        constexpr std::size_t kMaxSettingsBytes = 4u * 1024u * 1024u; // 4 MiB guardrail

        std::error_code ec;
        if (!winpath::read_file_to_string_with_retry(p, out, &ec, kMaxSettingsBytes, /*max_attempts=*/32))
            return false;

        // Treat empty files as "no settings".
        if (out.empty())
            return false;

        return true;
    }

    int ReadSettingsVersion(const nlohmann::json& j) noexcept
    {
        if (auto it = j.find("version"); it != j.end() && it->is_number_integer())
            return it->get<int>();
        return 0;
    }

    nlohmann::json& EnsureObject(nlohmann::json& j, const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_object())
        {
            j[key] = nlohmann::json::object();
            return j[key];
        }
        return *it;
    }

    // Best-effort migration for older settings layouts.
    //
    // We keep this intentionally forgiving: if keys exist in the expected new
    // locations we leave them alone, otherwise we look for legacy/root-level
    // equivalents and copy them into the modern nested objects.
    void NormalizeLegacySettingsJson(nlohmann::json& j, int fileVersion, bool& didMigrate)
    {
        if (!j.is_object())
            return;

        // Forward-compat: if the file is newer than we know, still try to read
        // the keys we understand.
        if (fileVersion > kUserSettingsSchemaVersion)
            return;

        if (fileVersion >= kUserSettingsSchemaVersion)
            return;

        auto& window   = EnsureObject(j, "window");
        auto& graphics = EnsureObject(j, "graphics");
        auto& runtime  = EnsureObject(j, "runtime");
        auto& input    = EnsureObject(j, "input");
        auto& debug    = EnsureObject(j, "debug");

        auto copy_if_missing = [&](const char* legacyKey, nlohmann::json& dst, const char* dstKey)
        {
            if (dst.contains(dstKey))
                return;
            auto it2 = j.find(legacyKey);
            if (it2 != j.end())
            {
                dst[dstKey] = *it2;
                didMigrate = true;
            }
        };

        // Common legacy layouts:
        //   - root-level keys (vsync/fullscreen/etc.) from early prototypes
        //   - windowWidth/windowHeight instead of window.width/window.height
        copy_if_missing("windowWidth",  window,   "width");
        copy_if_missing("windowHeight", window,   "height");
        copy_if_missing("width",        window,   "width");
        copy_if_missing("height",       window,   "height");

        // Window placement (newer schema stores these under window.*)
        copy_if_missing("windowPosValid", window, "posValid");
        copy_if_missing("windowPosX",     window, "posX");
        copy_if_missing("windowPosY",     window, "posY");
        copy_if_missing("windowMaximized", window, "maximized");

        copy_if_missing("vsync",              graphics, "vsync");
        copy_if_missing("fullscreen",         graphics, "fullscreen");
        copy_if_missing("maxFpsWhenVsyncOff",  graphics, "maxFpsWhenVsyncOff");
        copy_if_missing("maxFrameLatency",     graphics, "maxFrameLatency");
        copy_if_missing("swapchainScaling",    graphics, "swapchainScaling");

        // A couple of plausible historical aliases.
        copy_if_missing("swapchainScalingMode", graphics, "swapchainScaling");
        copy_if_missing("dxgiMaxFrameLatency",  graphics, "maxFrameLatency");

        copy_if_missing("pauseWhenUnfocused",  runtime,  "pauseWhenUnfocused");
        copy_if_missing("maxFpsWhenUnfocused", runtime,  "maxFpsWhenUnfocused");

        copy_if_missing("rawMouse",       input, "rawMouse");
        copy_if_missing("showFrameStats", debug, "showFrameStats");
        copy_if_missing("showDxgiDiagnostics", debug, "showDxgiDiagnostics");
        copy_if_missing("showDxgiDiag",        debug, "showDxgiDiagnostics");

        if (didMigrate)
        {
            // Bump the version in-memory so a re-save writes the latest schema.
            j["version"] = kUserSettingsSchemaVersion;
        }
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

    // Normalize to UTF-8 so the settings file can be edited in Notepad (UTF-16/with BOM) without breaking JSON parsing.
    if (!colony::util::NormalizeTextToUtf8(text))
        return false;

    // Allow // comments (same as input_bindings.json), and avoid exceptions.
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false, /*ignore_comments*/ true);
    if (j.is_discarded() || !j.is_object())
        return false;


    const int fileVersion = ReadSettingsVersion(j);
    bool didMigrate = false;
    NormalizeLegacySettingsJson(j, fileVersion, didMigrate);

    UserSettings tmp = out;

    if (const auto it = j.find("window"); it != j.end() && it->is_object())
    {
        if (auto w = it->find("width"); w != it->end() && w->is_number_integer())
            tmp.windowWidth = ClampWindowWidth(static_cast<std::uint32_t>(w->get<std::int64_t>()));
        if (auto h = it->find("height"); h != it->end() && h->is_number_integer())
            tmp.windowHeight = ClampWindowHeight(static_cast<std::uint32_t>(h->get<std::int64_t>()));

        if (auto pv = it->find("posValid"); pv != it->end() && pv->is_boolean())
            tmp.windowPosValid = pv->get<bool>();
        if (auto px = it->find("posX"); px != it->end() && px->is_number_integer())
            tmp.windowPosX = px->get<int>();
        if (auto py = it->find("posY"); py != it->end() && py->is_number_integer())
            tmp.windowPosY = py->get<int>();
        if (auto mz = it->find("maximized"); mz != it->end() && mz->is_boolean())
            tmp.windowMaximized = mz->get<bool>();
    }

    if (const auto it = j.find("graphics"); it != j.end() && it->is_object())
    {
        if (auto v = it->find("vsync"); v != it->end() && v->is_boolean())
            tmp.vsync = v->get<bool>();
        if (auto f = it->find("fullscreen"); f != it->end() && f->is_boolean())
            tmp.fullscreen = f->get<bool>();
        if (auto m = it->find("maxFpsWhenVsyncOff"); m != it->end() && m->is_number_integer())
            tmp.maxFpsWhenVsyncOff = ClampMaxFps(m->get<int>());

        if (auto fl = it->find("maxFrameLatency"); fl != it->end() && fl->is_number_integer())
            tmp.maxFrameLatency = ClampFrameLatency(fl->get<int>());

        if (auto sc = it->find("swapchainScaling"); sc != it->end())
            tmp.swapchainScaling = ParseScalingMode(*sc, tmp.swapchainScaling);
    }

    if (const auto it = j.find("runtime"); it != j.end() && it->is_object())
    {
        if (auto p = it->find("pauseWhenUnfocused"); p != it->end() && p->is_boolean())
            tmp.pauseWhenUnfocused = p->get<bool>();
        if (auto m = it->find("maxFpsWhenUnfocused"); m != it->end() && m->is_number_integer())
            tmp.maxFpsWhenUnfocused = ClampMaxFps(m->get<int>());
    }

    if (const auto it = j.find("input"); it != j.end() && it->is_object())
    {
        if (auto r = it->find("rawMouse"); r != it->end() && r->is_boolean())
            tmp.rawMouse = r->get<bool>();
    }

    if (const auto it = j.find("debug"); it != j.end() && it->is_object())
    {
        if (auto s = it->find("showFrameStats"); s != it->end() && s->is_boolean())
            tmp.showFrameStats = s->get<bool>();
        if (auto d = it->find("showDxgiDiagnostics"); d != it->end() && d->is_boolean())
            tmp.showDxgiDiagnostics = d->get<bool>();
    }

    if (didMigrate)
    {
        // Rewrite settings.json in the latest schema so future loads are fast
        // and older keys don't linger forever.
        (void)SaveUserSettings(tmp);
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
    j["version"] = kUserSettingsSchemaVersion;
    j["window"] = {
        {"width", settings.windowWidth},
        {"height", settings.windowHeight},
        {"posValid", settings.windowPosValid},
        {"posX", settings.windowPosX},
        {"posY", settings.windowPosY},
        {"maximized", settings.windowMaximized},
    };
    j["graphics"] = {
        {"vsync", settings.vsync},
        {"fullscreen", settings.fullscreen},
        {"maxFpsWhenVsyncOff", ClampMaxFps(settings.maxFpsWhenVsyncOff)},
        {"maxFrameLatency", ClampFrameLatency(settings.maxFrameLatency)},
        {"swapchainScaling", ScalingModeToString(settings.swapchainScaling)},
    };

    j["runtime"] = {
        {"pauseWhenUnfocused", settings.pauseWhenUnfocused},
        {"maxFpsWhenUnfocused", ClampMaxFps(settings.maxFpsWhenUnfocused)},
    };

    j["input"] = {
        {"rawMouse", settings.rawMouse},
    };

    j["debug"] = {
        {"showFrameStats", settings.showFrameStats},
        {"showDxgiDiagnostics", settings.showDxgiDiagnostics},
    };

    std::string payload = j.dump(4);
    payload.push_back('\n');

    return winpath::atomic_write_file(UserSettingsPath(), payload);
}

} // namespace colony::appwin
