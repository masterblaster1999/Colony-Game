#include "game/proto/ProtoWorld.h"
#include "game/proto/ProtoWorld_SaveFormat.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

#include "platform/win/PathUtilWin.h"
#include "util/TextEncoding.h"

#include <nlohmann/json.hpp>

namespace colony::proto {

namespace {

using json = nlohmann::json;

[[nodiscard]] TileType TileFromInt(int v) noexcept
{
    if (v < 0) v = 0;
    if (v > static_cast<int>(TileType::Stockpile))
        v = static_cast<int>(TileType::Stockpile);
    return static_cast<TileType>(static_cast<std::uint8_t>(v));
}

[[nodiscard]] bool IsNumber(const json& v) noexcept
{
    return v.is_number_integer() || v.is_number_unsigned() || v.is_number_float();
}

[[nodiscard]] int SafeInt(const json& v, int def) noexcept
{
    if (!IsNumber(v))
        return def;

    // Prefer integer if possible; otherwise clamp-cast.
    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_number_unsigned())
        return static_cast<int>(v.get<std::uint64_t>());
    return static_cast<int>(v.get<double>());
}

[[nodiscard]] float SafeFloat(const json& v, float def) noexcept
{
    if (!IsNumber(v))
        return def;
    return static_cast<float>(v.get<double>());
}

[[nodiscard]] int ObjInt(const json& obj, const char* key, int def) noexcept
{
    if (!obj.is_object())
        return def;
    auto it = obj.find(key);
    if (it == obj.end())
        return def;
    return SafeInt(*it, def);
}

[[nodiscard]] double ObjDouble(const json& obj, const char* key, double def) noexcept
{
    if (!obj.is_object())
        return def;
    auto it = obj.find(key);
    if (it == obj.end())
        return def;
    if (!IsNumber(*it))
        return def;
    return it->get<double>();
}

[[nodiscard]] std::string ObjString(const json& obj, const char* key, const std::string& def) noexcept
{
    if (!obj.is_object())
        return def;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return def;
    return it->get<std::string>();
}

[[nodiscard]] int ArrInt(const json& arr, std::size_t idx, int def) noexcept
{
    if (!arr.is_array() || idx >= arr.size())
        return def;
    return SafeInt(arr[idx], def);
}

[[nodiscard]] float ArrFloat(const json& arr, std::size_t idx, float def) noexcept
{
    if (!arr.is_array() || idx >= arr.size())
        return def;
    return SafeFloat(arr[idx], def);
}

[[nodiscard]] bool ReadFileToStringRobust(const std::filesystem::path& path,
                                         std::string& out,
                                         std::string* outError) noexcept
{
    constexpr std::size_t kMaxSaveBytes = 512u * 1024u * 1024u; // 512 MiB guardrail

    std::error_code ec;
    if (!winpath::read_file_to_string_with_retry(path, out, &ec, kMaxSaveBytes, /*max_attempts=*/64))
    {
        if (outError)
        {
            *outError = "Failed to read save file";
            if (ec)
            {
                *outError += ": ";
                *outError += ec.message();
                *outError += " (code ";
                *outError += std::to_string(ec.value());
                *outError += ")";
            }
            *outError += ".";
        }
        return false;
    }
    return true;
}

} // namespace

bool World::SaveJson(const std::filesystem::path& path, std::string* outError) const noexcept
{
    try
    {
        using json = nlohmann::json;

        json j;
        j["format"] = savefmt::kWorldFormat;
        j["version"] = savefmt::kWorldVersion;

        j["size"] = { {"w", m_w}, {"h", m_h} };

        j["inventory"] = {
            {"wood", m_inv.wood},
            {"food", m_inv.food},
        };

        j["tuning"] = {
            {"buildWorkPerSecond", buildWorkPerSecond},
            {"colonistWalkSpeed", colonistWalkSpeed},
            {"farmFoodPerSecond", farmFoodPerSecond},
            {"foodPerColonistPerSecond", foodPerColonistPerSecond},
        };

        json cells = json::array();
        cells.get_ref<json::array_t&>().reserve(m_cells.size());
        for (const Cell& c : m_cells)
        {
            cells.push_back({
                static_cast<int>(c.built),
                static_cast<int>(c.planned),
                c.workRemaining,
                static_cast<int>(c.planPriority),
            });
        }
        j["cells"] = std::move(cells);

        json colonists = json::array();
        colonists.get_ref<json::array_t&>().reserve(m_colonists.size());
        for (const Colonist& c : m_colonists)
            colonists.push_back({ {"id", c.id}, {"x", c.x}, {"y", c.y} });
        j["colonists"] = std::move(colonists);

        const std::string bytes = j.dump(2);

        std::error_code wec;
        if (!winpath::atomic_write_file(path, bytes.data(), bytes.size(), &wec))
        {
            if (outError)
            {
                *outError = "Failed to write save file";
                if (wec)
                {
                    *outError += ": ";
                    *outError += wec.message();
                    *outError += " (code ";
                    *outError += std::to_string(wec.value());
                    *outError += ")";
                }
                *outError += ".";
            }
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (outError) *outError = e.what();
        return false;
    }
    catch (...)
    {
        if (outError) *outError = "Unknown error while saving.";
        return false;
    }
}

bool World::LoadJson(const std::filesystem::path& path, std::string* outError) noexcept
{
    try
    {
        std::string bytes;
        std::string readErr;
        if (!ReadFileToStringRobust(path, bytes, &readErr))
        {
            if (outError) *outError = readErr;
            return false;
        }

        if (!colony::util::NormalizeTextToUtf8(bytes))
        {
            if (outError) *outError = "Save file encoding is not valid UTF-8/UTF-16.";
            return false;
        }

        json j = json::parse(bytes, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
        if (j.is_discarded() || !j.is_object())
        {
            if (outError) *outError = "Save file is not valid JSON.";
            return false;
        }

        // Basic format/version check (best-effort; allow missing fields for early prototypes).
        if (j.contains("format"))
        {
            const std::string fmt = ObjString(j, "format", std::string{});
            if (fmt != savefmt::kWorldFormat && fmt != savefmt::kWorldFormatLegacy)
            {
                if (outError) *outError = "Unsupported save format.";
                return false;
            }
        }

        const int version = ObjInt(j, "version", 1);
        if (version != 1 && version != 2)
        {
            if (outError) *outError = "Unsupported save version.";
            return false;
        }

        int w = m_w;
        int h = m_h;
        if (j.contains("size") && j["size"].is_object())
        {
            const json& size = j["size"];
            w = ObjInt(size, "w", w);
            h = ObjInt(size, "h", h);
        }
        if (w <= 0 || h <= 0)
        {
            if (outError) *outError = "Invalid world size.";
            return false;
        }

        // Reset to allocate buffers (we overwrite content below).
        reset(w, h, /*seed=*/1);

        // Inventory
        if (j.contains("inventory") && j["inventory"].is_object())
        {
            const json& inv = j["inventory"];
            m_inv.wood = ObjInt(inv, "wood", m_inv.wood);
            m_inv.food = static_cast<float>(ObjDouble(inv, "food", m_inv.food));
        }

        // Tuning
        if (j.contains("tuning") && j["tuning"].is_object())
        {
            const json& t = j["tuning"];
            buildWorkPerSecond = ObjDouble(t, "buildWorkPerSecond", buildWorkPerSecond);
            colonistWalkSpeed = ObjDouble(t, "colonistWalkSpeed", colonistWalkSpeed);
            farmFoodPerSecond = ObjDouble(t, "farmFoodPerSecond", farmFoodPerSecond);
            foodPerColonistPerSecond = ObjDouble(t, "foodPerColonistPerSecond", foodPerColonistPerSecond);
        }

        // Cells
        if (j.contains("cells") && j["cells"].is_array())
        {
            const json& cells = j["cells"];
            const std::size_t expected = static_cast<std::size_t>(m_w * m_h);
            if (cells.size() != expected)
            {
                if (outError) *outError = "Save file has wrong cell count.";
                return false;
            }

            std::size_t i = 0;
            for (int y = 0; y < m_h; ++y)
            {
                for (int x = 0; x < m_w; ++x)
                {
                    Cell& c = cell(x, y);
                    c.reservedBy = -1; // always clear reservations on load

                    const json& entry = cells[i++];
                    if (!entry.is_array() || entry.size() < 2)
                    {
                        c.built = TileType::Empty;
                        c.planned = TileType::Empty;
                        c.planPriority = 0;
                        c.workRemaining = 0.0f;
                        continue;
                    }

                    c.built = TileFromInt(ArrInt(entry, 0, 0));
                    c.planned = TileFromInt(ArrInt(entry, 1, 0));

                    c.workRemaining = ArrFloat(entry, 2, 0.0f);

                    // v2+ includes planPriority (0..3). Default to 0 for v1.
                    int pr = ArrInt(entry, 3, 0);
                    pr = std::max(0, std::min(3, pr));
                    c.planPriority = static_cast<std::uint8_t>(pr);

                    // Sanitize: priority/work are only meaningful for active plans.
                    if (c.planned == TileType::Empty || c.planned == c.built)
                    {
                        c.planPriority = 0;
                        c.workRemaining = 0.0f;
                    }
                }
            }
        }

        // Colonists
        m_colonists.clear();
        if (j.contains("colonists") && j["colonists"].is_array())
        {
            const json& arr = j["colonists"];
            for (const auto& item : arr)
            {
                if (!item.is_object())
                    continue;

                Colonist c;
                c.id = ObjInt(item, "id", static_cast<int>(m_colonists.size()));
                c.x = static_cast<float>(ObjDouble(item, "x", 0.5));
                c.y = static_cast<float>(ObjDouble(item, "y", 0.5));

                // Clear job/path (reassigned on next tick).
                c.hasJob = false;
                c.targetX = 0;
                c.targetY = 0;
                c.path.clear();
                c.pathIndex = 0;

                // Clamp inside world bounds (in tile coordinates).
                if (c.x < 0.0f) c.x = 0.0f;
                if (c.y < 0.0f) c.y = 0.0f;
                const float maxX = static_cast<float>(m_w) - 0.5f;
                const float maxY = static_cast<float>(m_h) - 0.5f;
                if (c.x > maxX) c.x = maxX;
                if (c.y > maxY) c.y = maxY;

                m_colonists.push_back(std::move(c));
            }
        }

        // Rebuild derived caches (nav + planned cache).
        syncAllNav();
        rebuildPlannedCache();
        rebuildBuiltCounts();

        // Allow job assignment immediately after a load.
        m_jobAssignCooldown = 0.0;

        return true;
    }
    catch (const std::exception& e)
    {
        if (outError) *outError = e.what();
        return false;
    }
    catch (...)
    {
        if (outError) *outError = "Unknown error while loading.";
        return false;
    }
}

} // namespace colony::proto
