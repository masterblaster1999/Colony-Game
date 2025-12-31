#include "game/editor/BlueprintLibrary.h"

#include "platform/win/PathUtilWin.h"
#include "util/PathUtf8.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

namespace colony::game::editor {

namespace {

constexpr const char* kBlueprintExt = ".blueprint.json";

[[nodiscard]] bool EndsWithInsensitive(std::string_view s, std::string_view suffix) noexcept
{
    if (suffix.size() > s.size())
        return false;

    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(s[off + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

[[nodiscard]] std::int64_t FileTimeToUnixSecondsUtc(const fs::file_time_type& ft) noexcept
{
    // file_time_type may have an arbitrary epoch/clock. Convert to system_clock.
    // This common technique avoids platform-specific APIs.
    using namespace std::chrono;

    const auto nowFile = fs::file_time_type::clock::now();
    const auto nowSys  = system_clock::now();

    const auto delta = ft - nowFile;
    const auto sysTp = nowSys + duration_cast<system_clock::duration>(delta);

    const auto secs = time_point_cast<seconds>(sysTp).time_since_epoch();
    return duration_cast<seconds>(secs).count();
}

[[nodiscard]] bool IsReservedDeviceName(std::string_view upper) noexcept
{
    // Windows reserved device names (case-insensitive): CON, PRN, AUX, NUL, COM1..COM9, LPT1..LPT9
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
        return true;

    auto starts_with = [](std::string_view s, std::string_view p) {
        return s.size() >= p.size() && s.substr(0, p.size()) == p;
    };

    if (starts_with(upper, "COM") || starts_with(upper, "LPT"))
    {
        if (upper.size() == 4)
        {
            const char d = upper[3];
            return (d >= '1' && d <= '9');
        }
    }
    return false;
}

[[nodiscard]] std::string ToUpperAscii(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return out;
}

} // namespace

std::string SanitizeBlueprintName(std::string_view name) noexcept
{
    // Trim leading/trailing whitespace.
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    std::size_t b = 0;
    std::size_t e = name.size();
    while (b < e && is_space(static_cast<unsigned char>(name[b]))) ++b;
    while (e > b && is_space(static_cast<unsigned char>(name[e - 1]))) --e;

    name = name.substr(b, e - b);

    std::string out;
    out.reserve(name.size());

    for (const char ch : name)
    {
        const unsigned char c = static_cast<unsigned char>(ch);

        // Allow common safe filename chars. Replace everything else with '_'.
        if (std::isalnum(c) || ch == '-' || ch == '_' || ch == '.' || ch == ' ')
            out.push_back(ch);
        else
            out.push_back('_');
    }

    // Windows doesn't allow trailing '.' or ' ' in filenames.
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();

    // Collapse repeated spaces.
    {
        std::string compact;
        compact.reserve(out.size());
        bool prevSpace = false;
        for (char ch : out)
        {
            const bool space = (ch == ' ');
            if (space && prevSpace)
                continue;
            compact.push_back(ch);
            prevSpace = space;
        }
        out.swap(compact);
    }

    if (out.empty())
        out = "blueprint";

    // Avoid reserved device names on Windows.
    const std::string upper = ToUpperAscii(out);
    if (IsReservedDeviceName(upper))
        out = "_" + out;

    return out;
}

fs::path BlueprintPathForName(const fs::path& dir, std::string_view name) noexcept
{
    std::string base = SanitizeBlueprintName(name);

    // If the user pasted a filename, strip known suffixes before re-appending our extension.
    if (EndsWithInsensitive(base, kBlueprintExt))
        base.resize(base.size() - std::string_view(kBlueprintExt).size());
    else if (EndsWithInsensitive(base, ".json"))
        base.resize(base.size() - std::string_view(".json").size());

    if (base.empty())
        base = "blueprint";
    fs::path p = dir;
    p /= base + kBlueprintExt;
    return p;
}

bool EnsureBlueprintDir(const fs::path& dir, std::string* outError) noexcept
{
    std::error_code ec;
    if (dir.empty())
    {
        if (outError)
            *outError = "Blueprint directory path is empty.";
        return false;
    }

    if (fs::exists(dir, ec))
    {
        if (ec)
        {
            if (outError)
                *outError = "Failed to query blueprint directory.";
            return false;
        }
        return true;
    }

    fs::create_directories(dir, ec);
    if (ec)
    {
        if (outError)
            *outError = "Failed to create blueprint directory.";
        return false;
    }
    return true;
}

std::vector<BlueprintFileInfo> ListBlueprintFiles(const fs::path& dir) noexcept
{
    std::vector<BlueprintFileInfo> out;

    std::error_code ec;
    if (!fs::exists(dir, ec) || ec)
        return out;

    for (const fs::directory_entry& de : fs::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        if (!de.is_regular_file(ec) || ec)
            continue;

        const fs::path p = de.path();

        const std::string fn = colony::util::PathToUtf8String(p.filename());
        if (!EndsWithInsensitive(fn, kBlueprintExt))
            continue;

        BlueprintFileInfo info;
        info.path = p;

        // Strip extension(s) for display.
        // filename: <name>.blueprint.json
        std::string base = colony::util::PathToUtf8String(p.filename());
        if (EndsWithInsensitive(base, kBlueprintExt))
            base.resize(base.size() - std::string_view(kBlueprintExt).size());

        info.name = base;

        info.sizeBytes = de.file_size(ec);
        if (ec)
        {
            ec.clear();
            info.sizeBytes = 0;
        }

        const auto ft = de.last_write_time(ec);
        if (!ec)
            info.modifiedUtcSeconds = FileTimeToUnixSecondsUtc(ft);
        else
            info.modifiedUtcSeconds = 0;

        out.push_back(std::move(info));
    }

    // Sort newest-first (unknown timestamps last).
    std::sort(out.begin(), out.end(), [](const BlueprintFileInfo& a, const BlueprintFileInfo& b) {
        if (a.modifiedUtcSeconds == b.modifiedUtcSeconds)
            return a.name < b.name;
        if (a.modifiedUtcSeconds == 0)
            return false;
        if (b.modifiedUtcSeconds == 0)
            return true;
        return a.modifiedUtcSeconds > b.modifiedUtcSeconds;
    });

    return out;
}

bool SaveBlueprintToFile(const PlanBlueprint& bp, const fs::path& path, std::string* outError) noexcept
{
    if (bp.Empty())
    {
        if (outError)
            *outError = "Blueprint is empty.";
        return false;
    }

    if (path.empty())
    {
        if (outError)
            *outError = "Blueprint path is empty.";
        return false;
    }

    std::error_code ec;
    const fs::path parent = path.parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    const std::string json = PlanBlueprintToJson(bp);
    if (json.empty())
    {
        if (outError)
            *outError = "Failed to serialize blueprint.";
        return false;
    }

    if (!winpath::atomic_write_file(path, json))
    {
        if (outError)
            *outError = "Failed to write blueprint file.";
        return false;
    }

    return true;
}

bool LoadBlueprintFromFile(const fs::path& path, PlanBlueprint& out, std::string* outError) noexcept
{
    if (path.empty())
    {
        if (outError)
            *outError = "Blueprint path is empty.";
        return false;
    }

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        if (outError)
            *outError = "Blueprint file does not exist.";
        return false;
    }

    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f)
    {
        if (outError)
            *outError = "Failed to open blueprint file.";
        return false;
    }

    std::string text;
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz > 0)
        text.resize(static_cast<std::size_t>(sz));
    f.seekg(0, std::ios::beg);
    if (!text.empty())
        f.read(text.data(), static_cast<std::streamsize>(text.size()));

    if (!f && !f.eof())
    {
        if (outError)
            *outError = "Failed to read blueprint file.";
        return false;
    }

    return PlanBlueprintFromJson(text, out, outError);
}

bool DeleteBlueprintFile(const fs::path& path, std::string* outError) noexcept
{
    if (path.empty())
    {
        if (outError)
            *outError = "Blueprint path is empty.";
        return false;
    }

    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    if (ec || !removed)
    {
        if (outError)
            *outError = "Failed to delete blueprint file.";
        return false;
    }

    return true;
}

} // namespace colony::game::editor
