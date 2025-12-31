#include "game/editor/Blueprint.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace colony::game::editor {

namespace {

using json = nlohmann::json;

constexpr int kBlueprintVersion = 1;

[[nodiscard]] bool IsNumber(const json& v) noexcept
{
    return v.is_number_integer() || v.is_number_unsigned() || v.is_number_float();
}

[[nodiscard]] int SafeInt(const json& v, int def) noexcept
{
    if (!IsNumber(v))
        return def;

    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_number_unsigned())
        return static_cast<int>(v.get<std::uint64_t>());
    return static_cast<int>(v.get<double>());
}

[[nodiscard]] std::uint8_t SanitizePacked(int v) noexcept
{
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;

    const std::uint8_t b = static_cast<std::uint8_t>(v);
    const std::uint8_t tile = static_cast<std::uint8_t>(b & 0x0Fu);
    const std::uint8_t prio = static_cast<std::uint8_t>((b >> 4) & 0x03u);

    const std::uint8_t maxTile = static_cast<std::uint8_t>(colony::proto::TileType::Remove);
    const std::uint8_t safeTile = (tile <= maxTile) ? tile : static_cast<std::uint8_t>(colony::proto::TileType::Empty);

    return static_cast<std::uint8_t>((safeTile & 0x0Fu) | ((prio & 0x03u) << 4));
}

} // namespace

void PlanBlueprint::Clear() noexcept
{
    w = 0;
    h = 0;
    packed.clear();
}

bool PlanBlueprint::Empty() const noexcept
{
    return w <= 0 || h <= 0 || packed.empty();
}

std::uint8_t BlueprintPack(colony::proto::TileType t, std::uint8_t priority) noexcept
{
    const std::uint8_t tile = static_cast<std::uint8_t>(t) & 0x0Fu;
    const std::uint8_t pr   = std::min<std::uint8_t>(priority, 3u);
    return static_cast<std::uint8_t>(tile | ((pr & 0x03u) << 4));
}

colony::proto::TileType BlueprintUnpackTile(std::uint8_t p) noexcept
{
    const std::uint8_t tile = static_cast<std::uint8_t>(p & 0x0Fu);
    const std::uint8_t maxTile = static_cast<std::uint8_t>(colony::proto::TileType::Remove);
    if (tile > maxTile)
        return colony::proto::TileType::Empty;
    return static_cast<colony::proto::TileType>(tile);
}

std::uint8_t BlueprintUnpackPriority(std::uint8_t p) noexcept
{
    return static_cast<std::uint8_t>((p >> 4) & 0x03u);
}

std::string PlanBlueprintToJson(const PlanBlueprint& bp)
{
    json j;
    j["type"]    = "colony_plan_blueprint";
    j["version"] = kBlueprintVersion;
    j["w"]       = bp.w;
    j["h"]       = bp.h;

    json rle = json::array();

    if (!bp.packed.empty())
    {
        std::uint8_t cur = bp.packed[0];
        int count = 1;

        for (std::size_t i = 1; i < bp.packed.size(); ++i)
        {
            const std::uint8_t v = bp.packed[i];
            if (v == cur && count < std::numeric_limits<int>::max())
            {
                ++count;
                continue;
            }

            rle.push_back(json::array({count, static_cast<int>(cur)}));
            cur   = v;
            count = 1;
        }

        rle.push_back(json::array({count, static_cast<int>(cur)}));
    }

    j["rle"] = std::move(rle);
    return j.dump();
}

bool PlanBlueprintFromJson(std::string_view text, PlanBlueprint& out, std::string* outError) noexcept
{
    out.Clear();

    json j;
    try {
        j = json::parse(text.begin(), text.end());
    }
    catch (const std::exception& e)
    {
        if (outError)
            *outError = std::string("JSON parse failed: ") + e.what();
        return false;
    }

    if (!j.is_object())
    {
        if (outError)
            *outError = "Blueprint JSON must be an object.";
        return false;
    }

    const int version = j.value("version", 0);
    if (version != kBlueprintVersion)
    {
        if (outError)
            *outError = "Unsupported blueprint version.";
        return false;
    }

    const int w = j.value("w", 0);
    const int h = j.value("h", 0);
    if (w <= 0 || h <= 0)
    {
        if (outError)
            *outError = "Blueprint w/h must be positive.";
        return false;
    }

    if (expected > (1u << 24))
    {
        if (outError)
            *outError = "Blueprint is too large.";
        return false;
    }

    // New format: RLE.
    if (j.contains("rle"))
    {
        const auto& rle = j["rle"];
        if (!rle.is_array())
        {
            if (outError)
                *outError = "Blueprint rle must be an array.";
            return false;
        }

        out.w = w;
        out.h = h;
        out.packed.clear();
        out.packed.reserve(expected);

        for (const auto& run : rle)
        {
            if (!run.is_array() || run.size() < 2)
                continue;

            const int count = SafeInt(run[0], 0);
            const int value = SafeInt(run[1], 0);
            if (count <= 0)
                continue;

            const std::uint8_t packed = SanitizePacked(value);

            const std::size_t remaining = expected - out.packed.size();
            const std::size_t emit = std::min<std::size_t>(remaining, static_cast<std::size_t>(count));
            out.packed.insert(out.packed.end(), emit, packed);

            if (out.packed.size() >= expected)
                break;
        }

        if (out.packed.size() != expected)
        {
            if (outError)
                *outError = "Blueprint payload size mismatch.";
            out.Clear();
            return false;
        }

        return true;
    }

    // Back-compat: raw cell array (not RLE).
    if (j.contains("cells"))
    {
        const auto& cells = j["cells"];
        if (!cells.is_array())
        {
            if (outError)
                *outError = "Blueprint cells must be an array.";
            return false;
        }

        if (cells.size() != expected)
        {
            if (outError)
                *outError = "Blueprint cells length mismatch.";
            return false;
        }

        out.w = w;
        out.h = h;
        out.packed.resize(expected);

        for (std::size_t i = 0; i < expected; ++i)
            out.packed[i] = SanitizePacked(SafeInt(cells[i], 0));

        return true;
    }

    if (outError)
        *outError = "Blueprint JSON missing 'rle' (or legacy 'cells') field.";
    return false;
}


PlanBlueprint BlueprintRotateCW(const PlanBlueprint& bp) noexcept
{
    PlanBlueprint out;
    if (bp.Empty())
        return out;

    const int w = bp.w;
    const int h = bp.h;
    if (w <= 0 || h <= 0)
        return out;


    out.w = h;
    out.h = w;
    out.packed.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), std::uint8_t{0});

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t v = (src < bp.packed.size()) ? bp.packed[src] : std::uint8_t{0};

            // (x, y) -> (h-1-y, x)
            const int nx = h - 1 - y;
            const int ny = x;
            const std::size_t dst = static_cast<std::size_t>(ny) * static_cast<std::size_t>(out.w) + static_cast<std::size_t>(nx);
            if (dst < out.packed.size())
                out.packed[dst] = v;
        }
    }

    return out;
}

PlanBlueprint BlueprintRotateCCW(const PlanBlueprint& bp) noexcept
{
    PlanBlueprint out;
    if (bp.Empty())
        return out;

    const int w = bp.w;
    const int h = bp.h;
    if (w <= 0 || h <= 0)
        return out;

    out.w = h;
    out.h = w;
    out.packed.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), std::uint8_t{0});

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t v = (src < bp.packed.size()) ? bp.packed[src] : std::uint8_t{0};

            // (x, y) -> (y, w-1-x)
            const int nx = y;
            const int ny = w - 1 - x;
            const std::size_t dst = static_cast<std::size_t>(ny) * static_cast<std::size_t>(out.w) + static_cast<std::size_t>(nx);
            if (dst < out.packed.size())
                out.packed[dst] = v;
        }
    }

    return out;
}

PlanBlueprint BlueprintRotate180(const PlanBlueprint& bp) noexcept
{
    PlanBlueprint out;
    if (bp.Empty())
        return out;

    const int w = bp.w;
    const int h = bp.h;
    if (w <= 0 || h <= 0)
        return out;

    out.w = w;
    out.h = h;
    out.packed.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), std::uint8_t{0});

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t v = (src < bp.packed.size()) ? bp.packed[src] : std::uint8_t{0};

            // (x, y) -> (w-1-x, h-1-y)
            const int nx = w - 1 - x;
            const int ny = h - 1 - y;
            const std::size_t dst = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
            if (dst < out.packed.size())
                out.packed[dst] = v;
        }
    }

    return out;
}

PlanBlueprint BlueprintFlipHorizontal(const PlanBlueprint& bp) noexcept
{
    PlanBlueprint out;
    if (bp.Empty())
        return out;

    const int w = bp.w;
    const int h = bp.h;
    if (w <= 0 || h <= 0)
        return out;

    out.w = w;
    out.h = h;
    out.packed.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), std::uint8_t{0});

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t v = (src < bp.packed.size()) ? bp.packed[src] : std::uint8_t{0};

            const int nx = w - 1 - x;
            const int ny = y;
            const std::size_t dst = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
            if (dst < out.packed.size())
                out.packed[dst] = v;
        }
    }

    return out;
}

PlanBlueprint BlueprintFlipVertical(const PlanBlueprint& bp) noexcept
{
    PlanBlueprint out;
    if (bp.Empty())
        return out;

    const int w = bp.w;
    const int h = bp.h;
    if (w <= 0 || h <= 0)
        return out;

    out.w = w;
    out.h = h;
    out.packed.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), std::uint8_t{0});

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t v = (src < bp.packed.size()) ? bp.packed[src] : std::uint8_t{0};

            const int nx = x;
            const int ny = h - 1 - y;
            const std::size_t dst = static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx);
            if (dst < out.packed.size())
                out.packed[dst] = v;
        }
    }

    return out;
}

} // namespace colony::game::editor
