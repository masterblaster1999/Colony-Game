#include "app/CommandLineArgs.h"

#include "platform/win/WinCommon.h"

#include <shellapi.h> // CommandLineToArgvW

#include <algorithm>
#include <cwctype>
#include <limits>
#include <sstream>
#include <string_view>

namespace colony::appwin {

namespace {

[[nodiscard]] std::wstring ToLower(std::wstring_view s)
{
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s)
        out.push_back(static_cast<wchar_t>(std::towlower(c)));
    return out;
}

[[nodiscard]] bool StartsWith(std::wstring_view s, std::wstring_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool ConsumeValue(std::wstring_view arg,
                               std::wstring_view prefix,
                               std::wstring_view& outValue)
{
    if (!StartsWith(arg, prefix))
        return false;

    // Accept either:
    //   --opt=value
    //   --opt:value
    // (Windows conventions often use ':' as well.)
    const std::size_t n = prefix.size();
    if (arg.size() == n)
        return false;

    const wchar_t sep = arg[n];
    if (sep != L'=' && sep != L':')
        return false;

    outValue = arg.substr(n + 1);
    return true;
}

[[nodiscard]] std::optional<int> ParseInt(std::wstring_view s)
{
    if (s.empty())
        return std::nullopt;

    int sign = 1;
    std::size_t i = 0;
    if (s[0] == L'+') {
        i = 1;
    } else if (s[0] == L'-') {
        sign = -1;
        i = 1;
    }

    long long v = 0;
    for (; i < s.size(); ++i)
    {
        const wchar_t c = s[i];
        if (c < L'0' || c > L'9')
            return std::nullopt;
        v = v * 10 + static_cast<long long>(c - L'0');
        if (v > 1'000'000'000LL)
            return std::nullopt; // absurd
    }

    v *= sign;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        return std::nullopt;

    return static_cast<int>(v);
}

} // namespace

CommandLineArgs ParseCommandLineArgs()
{
    CommandLineArgs out;

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
        return out;
    if (argc <= 1)
    {
        ::LocalFree(argv);
        return out;
    }

    auto addUnknown = [&](std::wstring_view raw) {
        out.unknown.emplace_back(raw);
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring_view raw(argv[i]);
        if (raw.empty())
            continue;
        // Normalize Windows-style /foo switches:
        //   /foo        -> --foo
        //   /foo:bar    -> --foo:bar     (':' is supported by ConsumeValue)
        //   /w 1280     -> -w 1280       (keep existing short aliases)
        //
        // This keeps Windows command lines ergonomic without duplicating the parser.
        std::wstring normalized(raw);
        if (!normalized.empty() && normalized[0] == L'/')
        {
            std::wstring_view rest(normalized);
            rest.remove_prefix(1);

            auto isSlashAlias = [&](std::wstring_view alias) -> bool {
                if (rest.size() < alias.size())
                    return false;
                if (rest.substr(0, alias.size()) != alias)
                    return false;
                if (rest.size() == alias.size())
                    return true;
                const wchar_t next = rest[alias.size()];
                return next == L':' || next == L'=';
            };

            if (isSlashAlias(L"?") || isSlashAlias(L"h") || isSlashAlias(L"w") ||
                isSlashAlias(L"hgt") || isSlashAlias(L"mfl") || isSlashAlias(L"fps") ||
                isSlashAlias(L"bgfps"))
            {
                // /w -> -w, etc.
                normalized[0] = L'-';
            }
            else
            {
                // /safe-mode -> --safe-mode
                normalized.erase(normalized.begin());
                normalized.insert(0, L"--");
            }
        }

        const std::wstring lowered = ToLower(normalized);
        const std::wstring_view arg(lowered);

        // Help
        if (arg == L"--help" || arg == L"-h" || arg == L"/?" || arg == L"-?") {
            out.showHelp = true;
            continue;
        }

        // Simple flags
        if (arg == L"--safe-mode" || arg == L"--safe") { out.safeMode = true; continue; }
        if (arg == L"--reset-settings" || arg == L"--reset-config") { out.resetSettings = true; continue; }
        if (arg == L"--reset-imgui" || arg == L"--reset-ui") { out.resetImGui = true; continue; }
        if (arg == L"--reset-bindings" || arg == L"--reset-input-bindings" || arg == L"--reset-inputs" || arg == L"--reset-binds") { out.resetBindings = true; continue; }
        if (arg == L"--ignore-settings") { out.ignoreSettings = true; continue; }
        if (arg == L"--ignore-imgui-ini") { out.ignoreImGuiIni = true; continue; }
        if (arg == L"--no-imgui" || arg == L"--no-ui" || arg == L"--noimgui") { out.disableImGui = true; continue; }

        // Boolean overrides
        if (arg == L"--fullscreen") { out.fullscreen = true; continue; }
        if (arg == L"--windowed") { out.fullscreen = false; continue; }
        if (arg == L"--vsync") { out.vsync = true; continue; }
        if (arg == L"--novsync" || arg == L"--no-vsync") { out.vsync = false; continue; }
        if (arg == L"--rawmouse") { out.rawMouse = true; continue; }
        if (arg == L"--norawmouse" || arg == L"--no-rawmouse") { out.rawMouse = false; continue; }
        if (arg == L"--pause-when-unfocused" || arg == L"--pause-bg") { out.pauseWhenUnfocused = true; continue; }
        if (arg == L"--no-pause-when-unfocused" || arg == L"--no-pause-bg") { out.pauseWhenUnfocused = false; continue; }

        // Options with values
        std::wstring_view value;

        const auto takeNext = [&](std::optional<int>& dst) {
            if (i + 1 >= argc) {
                addUnknown(raw);
                return;
            }
            const auto parsed = ParseInt(argv[i + 1]);
            if (!parsed) {
                addUnknown(raw);
                return;
            }
            dst = *parsed;
            ++i;
        };

        const auto parseValueInto = [&](std::optional<int>& dst, std::wstring_view v) {
            const auto parsed = ParseInt(v);
            if (!parsed) {
                addUnknown(raw);
                return;
            }
            dst = *parsed;
        };

        if (arg == L"--width" || arg == L"-w") {
            takeNext(out.width);
            continue;
        }
        if (ConsumeValue(arg, L"--width", value) || ConsumeValue(arg, L"-w", value)) {
            parseValueInto(out.width, value);
            continue;
        }

        if (arg == L"--height" || arg == L"-hgt") {
            takeNext(out.height);
            continue;
        }
        if (ConsumeValue(arg, L"--height", value) || ConsumeValue(arg, L"-hgt", value)) {
            parseValueInto(out.height, value);
            continue;
        }

        if (arg == L"--max-frame-latency" || arg == L"--mfl") {
            takeNext(out.maxFrameLatency);
            continue;
        }
        if (ConsumeValue(arg, L"--max-frame-latency", value) || ConsumeValue(arg, L"--mfl", value)) {
            parseValueInto(out.maxFrameLatency, value);
            continue;
        }

        if (arg == L"--maxfps" || arg == L"--fps") {
            takeNext(out.maxFpsWhenVsyncOff);
            continue;
        }
        if (ConsumeValue(arg, L"--maxfps", value) || ConsumeValue(arg, L"--fps", value)) {
            parseValueInto(out.maxFpsWhenVsyncOff, value);
            continue;
        }

        if (arg == L"--bgfps" || arg == L"--background-fps") {
            takeNext(out.maxFpsWhenUnfocused);
            continue;
        }
        if (ConsumeValue(arg, L"--bgfps", value) || ConsumeValue(arg, L"--background-fps", value)) {
            parseValueInto(out.maxFpsWhenUnfocused, value);
            continue;
        }

        // Anything else is unknown.
        addUnknown(raw);
    }

    ::LocalFree(argv);
    return out;
}

std::wstring BuildCommandLineHelpText()
{
    std::wostringstream oss;
    oss << L"Colony Game - Command Line Options\n\n";
    oss << L"Recovery / troubleshooting\n";
    oss << L"  --safe-mode                 Run with defaults (ignore settings.json and imgui.ini)\n";
    oss << L"  --reset-settings             Delete %LOCALAPPDATA%\\ColonyGame\\settings.json\n";
    oss << L"  --reset-imgui                Delete %LOCALAPPDATA%\\ColonyGame\\imgui.ini\n";
    oss << L"  --reset-bindings             Delete per-user input_bindings.{json|ini} overrides\n";
    oss << L"  --ignore-settings            Don't read settings.json (does not delete it)\n";
    oss << L"  --ignore-imgui-ini            Don't read imgui.ini (does not delete it)\n";
    oss << L"  --no-imgui                    Disable ImGui overlay entirely\n\n";

    oss << L"Window / presentation overrides\n";
    oss << L"  --width <px>                 Initial window client width (e.g. 1280)\n";
    oss << L"  --height <px>                Initial window client height (e.g. 720)\n";
    oss << L"  --fullscreen / --windowed     Force start mode\n";
    oss << L"  --vsync / --novsync           Force VSync on/off\n";
    oss << L"  --rawmouse / --norawmouse     Force RAWINPUT mouse deltas on/off\n";
    oss << L"  --max-frame-latency <1..16>   Override DXGI max frame latency\n";
    oss << L"  --maxfps <0|N>                FPS cap used when VSync is OFF (0 = uncapped)\n";
    oss << L"  --pause-when-unfocused        Pause when in background (saves CPU/GPU)\n";
    oss << L"  --no-pause-when-unfocused     Keep running in background\n";
    oss << L"  --bgfps <0|N>                 Background FPS cap when not paused\n\n";

    oss << L"Misc\n";
    oss << L"  --help, -h, /?                Show this help\n\n";

    oss << L"Examples\n";
    oss << L"  ColonyGame.exe --safe-mode\n";
    oss << L"  ColonyGame.exe --reset-imgui\n";
    oss << L"  ColonyGame.exe --reset-bindings\n";
    oss << L"  ColonyGame.exe --windowed --novsync --maxfps 240\n";
    return oss.str();
}

} // namespace colony::appwin
