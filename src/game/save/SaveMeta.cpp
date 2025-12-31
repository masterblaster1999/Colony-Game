#include "game/save/SaveMeta.h"

#include "game/save/Base64.h"

#include "platform/win/PathUtilWin.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace colony::game::save {

namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxMetaFileBytes = 256u * 1024u;

[[nodiscard]] bool ReadSmallFileUtf8(const fs::path& path, std::string& out, std::string* outErr) noexcept
{
    out.clear();

    std::error_code ec;
    if (!winpath::read_file_to_string_with_retry(path, out, &ec, kMaxMetaFileBytes, /*max_attempts=*/32))
    {
        if (outErr)
        {
            *outErr = "Failed to read file";
            if (ec)
            {
                *outErr += ": ";
                *outErr += ec.message();
                *outErr += " (code ";
                *outErr += std::to_string(ec.value());
                *outErr += ")";
            }
            *outErr += ".";
        }
        out.clear();
        return false;
    }

    return true;
}


} // namespace

fs::path MetaPathFor(const fs::path& worldPath) noexcept
{
    fs::path p = worldPath;
    p.replace_extension(".meta.json");
    return p;
}

bool ReadMetaFile(const fs::path& metaPath,
                  SaveSummary& outSummary,
                  std::string* outError) noexcept
{
    outSummary = {};

    std::string text;
    std::string err;
    if (!ReadSmallFileUtf8(metaPath, text, &err))
    {
        if (outError) *outError = err;
        return false;
    }

    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        if (outError) *outError = "Meta JSON parse failed.";
        return false;
    }

    const std::string fmt = j.value("format", std::string{});
    if (fmt != "colony_proto_world_meta")
    {
        if (outError) *outError = "Unsupported meta format.";
        return false;
    }

    // meta
    if (j.contains("meta") && j["meta"].is_object())
    {
        const json& m = j["meta"];
        const std::string kind = m.value("kind", std::string{});
        if (kind == "manual") outSummary.kind = SaveKind::Manual;
        else if (kind == "autosave") outSummary.kind = SaveKind::Autosave;
        else outSummary.kind = SaveKind::Unknown;

        outSummary.savedUnixSecondsUtc = m.value("savedUnixSecondsUtc", static_cast<std::int64_t>(0));
        outSummary.playtimeSeconds     = m.value("playtimeSeconds", 0.0);
    }

    // world size
    if (j.contains("world") && j["world"].is_object())
    {
        const json& w = j["world"];
        outSummary.worldW = w.value("w", 0);
        outSummary.worldH = w.value("h", 0);
    }

    // inventory
    if (j.contains("inventory") && j["inventory"].is_object())
    {
        const json& inv = j["inventory"];
        outSummary.wood = inv.value("wood", 0);
        outSummary.food = inv.value("food", 0.0f);
    }

    // counts
    if (j.contains("counts") && j["counts"].is_object())
    {
        const json& c = j["counts"];
        outSummary.population   = c.value("population", 0);
        outSummary.plannedCount = c.value("planned", 0);

        if (c.contains("built") && c["built"].is_object())
        {
            const json& b = c["built"];
            outSummary.builtFloors     = b.value("Floor", 0);
            outSummary.builtWalls      = b.value("Wall", 0);
            outSummary.builtFarms      = b.value("Farm", 0);
            outSummary.builtStockpiles = b.value("Stockpile", 0);
        }
    }


    // thumbnail (optional save preview)
    if (j.contains("thumb") && j["thumb"].is_object())
    {
        const json& t = j["thumb"];
        const int tw = t.value("w", 0);
        const int th = t.value("h", 0);
        const std::string enc = t.value("encoding", std::string{});
        const std::string data = t.value("data", std::string{});

        // Hard guardrails: keep meta parsing cheap + safe even if the file is corrupted.
        if (tw > 0 && th > 0 && tw <= 256 && th <= 256 && !data.empty()
            && (enc == "base64_u8" || enc == "b64_u8"))
        {
            const std::size_t expected = static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);
            if (expected <= (256u * 256u))
            {
                std::vector<std::uint8_t> bytes;
                if (Base64Decode(data, bytes) && bytes.size() == expected)
                {
                    outSummary.thumbW = tw;
                    outSummary.thumbH = th;
                    outSummary.thumbPacked = std::move(bytes);
                }
            }
        }
    }
    return true;
}

std::string FormatLocalTime(std::int64_t unixSecondsUtc) noexcept
{
    if (unixSecondsUtc <= 0)
        return {};

    std::time_t tt = static_cast<std::time_t>(unixSecondsUtc);
    std::tm tm{};

#if defined(_WIN32)
    if (localtime_s(&tm, &tt) != 0)
        return {};
#else
    if (!localtime_r(&tt, &tm))
        return {};
#endif

    char buf[64] = {};
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm) == 0)
        return {};
    return std::string(buf);
}

std::string FormatDurationHMS(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;

    std::int64_t s = static_cast<std::int64_t>(seconds);
    const std::int64_t h = s / 3600;
    s %= 3600;
    const std::int64_t m = s / 60;
    s %= 60;

    char buf[64] = {};
    (void)std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld",
                        static_cast<long long>(h),
                        static_cast<long long>(m),
                        static_cast<long long>(s));
    return std::string(buf);
}

std::string FormatSummaryLine(const SaveSummary& s) noexcept
{
    std::ostringstream oss;

    // Kind
    switch (s.kind)
    {
    case SaveKind::Manual:   oss << "Manual"; break;
    case SaveKind::Autosave: oss << "Autosave"; break;
    default:                oss << "Save"; break;
    }

    // Playtime
    if (s.playtimeSeconds > 0.0)
        oss << " | " << FormatDurationHMS(s.playtimeSeconds);

    // World size
    if (s.worldW > 0 && s.worldH > 0)
        oss << " | " << s.worldW << "x" << s.worldH;

    // Counts
    if (s.population > 0)
        oss << " | Pop " << s.population;
    if (s.plannedCount > 0)
        oss << " | Plans " << s.plannedCount;

    // Inventory
    if (s.wood != 0)
        oss << " | Wood " << s.wood;
    if (s.food != 0.0f)
        oss << " | Food " << std::fixed << std::setprecision(1) << static_cast<double>(s.food);

    // Built snapshot (subset)
    const int builtSum = s.builtFloors + s.builtWalls + s.builtFarms + s.builtStockpiles;
    if (builtSum > 0)
    {
        oss << " | Built";
        if (s.builtFloors)     oss << " F:" << s.builtFloors;
        if (s.builtWalls)      oss << " W:" << s.builtWalls;
        if (s.builtFarms)      oss << " Farm:" << s.builtFarms;
        if (s.builtStockpiles) oss << " Stock:" << s.builtStockpiles;
    }

    return oss.str();
}

} // namespace colony::game::save
