#include "game/save/SaveMeta.h"

#include "game/save/Base64.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace colony::game::save {

namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxMetaFileBytes = 256u * 1024u;

[[nodiscard]] bool ReadSmallFileUtf8(const fs::path& path, std::string& out, std::string* outErr) noexcept
{
    out.clear();

    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        if (outErr) *outErr = "Failed to open file.";
        return false;
    }

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0)
    {
        if (outErr) *outErr = "Failed to query file size.";
        return false;
    }
    if (static_cast<std::size_t>(size) > kMaxMetaFileBytes)
    {
        if (outErr) *outErr = "Meta file is unexpectedly large.";
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    if (!out.empty())
        f.read(out.data(), static_cast<std::streamsize>(out.size()));

    if (!f && !out.empty())
    {
        if (outErr) *outErr = "Failed to read file contents.";
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

} // namespace colony::game::save
