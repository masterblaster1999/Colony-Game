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

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] TileType BuiltTileFromInt(int v) noexcept
{
    if (v < 0) v = 0;

    // v6+: allow Tree (and future built tiles) while still rejecting plan-only tile values.
    const int maxBuilt = static_cast<int>(TileType::Door);
    if (v > maxBuilt)
        v = maxBuilt;

    const TileType t = static_cast<TileType>(static_cast<std::uint8_t>(v));

    // Plan-only tile types are never valid as "built" data.
    if (t == TileType::Remove)
        return TileType::Empty;

    return t;
}

[[nodiscard]] TileType PlanTileFromInt(int v) noexcept
{
    if (v < 0) v = 0;

    // Plans can include any buildable tile type plus the plan-only "Remove".
    // (Tree plans are not exposed in the main UI, but we keep the format permissive for editor tools.)
    const int maxPlan = static_cast<int>(TileType::Door);
    if (v > maxPlan)
        v = maxPlan;

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


[[nodiscard]] bool SafeBool(const json& v, bool def) noexcept
{
    if (v.is_boolean())
        return v.get<bool>();
    if (IsNumber(v))
        return SafeInt(v, def ? 1 : 0) != 0;
    return def;
}

[[nodiscard]] bool ObjBool(const json& obj, const char* key, bool def) noexcept
{
    if (!obj.is_object())
        return def;
    auto it = obj.find(key);
    if (it == obj.end())
        return def;
    return SafeBool(*it, def);
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


[[nodiscard]] bool ArrBool(const json& arr, std::size_t idx, bool def) noexcept
{
    if (!arr.is_array() || idx >= arr.size())
        return def;
    return SafeBool(arr[idx], def);
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
            {"farmGrowDurationSeconds", farmGrowDurationSeconds},
            {"farmHarvestYieldFood", farmHarvestYieldFood},
            {"farmHarvestDurationSeconds", farmHarvestDurationSeconds},
            {"treeChopYieldWood", treeChopYieldWood},
            {"treeSpreadAttemptsPerSecond", treeSpreadAttemptsPerSecond},
            {"treeSpreadChancePerAttempt", treeSpreadChancePerAttempt},
            {"foodPerColonistPerSecond", foodPerColonistPerSecond},
            {"colonistMaxPersonalFood", colonistMaxPersonalFood},
            {"colonistEatThresholdFood", colonistEatThresholdFood},
            {"colonistEatDurationSeconds", colonistEatDurationSeconds},
            {"haulCarryCapacity", haulCarryCapacity},
            {"haulPickupDurationSeconds", haulPickupDurationSeconds},
            {"haulDropoffDurationSeconds", haulDropoffDurationSeconds},

            // v11+ pathfinding tuning
            {"pathfindingAlgorithm", std::string{PathAlgoName(pathAlgo)}},
            {"pathCacheEnabled", pathCacheEnabled},
            {"pathCacheMaxEntries", pathCacheMaxEntries},
            {"navTerrainCostsEnabled", navUseTerrainCosts},
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
                c.builtFromPlan ? 1 : 0,
                c.farmGrowth,
                c.looseWood,
            });
        }
        j["cells"] = std::move(cells);

        json colonists = json::array();
        colonists.get_ref<json::array_t&>().reserve(m_colonists.size());
                for (const Colonist& c : m_colonists)
        {
            json mq = json::array();
            mq.get_ref<json::array_t&>().reserve(c.manualQueue.size());
            for (const auto& o : c.manualQueue)
                mq.push_back({static_cast<int>(o.kind), o.x, o.y});

            colonists.push_back({
                {"id", c.id},
                {"x", c.x},
                {"y", c.y},
                {"personalFood", c.personalFood},
                {"drafted", c.drafted},

                // v7+: colonist roles + progression.
                {"role", RoleDefOf(c.role.role).name},
                {"roleLevel", static_cast<int>(c.role.level)},
                {"roleXp", static_cast<std::uint32_t>(c.role.xp)},

                // v9+: per-colonist work priorities.
                {"workPriorities", {
                    {"build", static_cast<int>(c.workPrio.build)},
                    {"farm", static_cast<int>(c.workPrio.farm)},
                    {"haul", static_cast<int>(c.workPrio.haul)},
                }},

                // v12+: drafted manual order queue.
                {"manualQueue", std::move(mq)},
            });
        }

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
        if (version < 1 || version > savefmt::kWorldVersion)
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
            farmGrowDurationSeconds = ObjDouble(t, "farmGrowDurationSeconds", farmGrowDurationSeconds);
            farmHarvestYieldFood = ObjDouble(t, "farmHarvestYieldFood", farmHarvestYieldFood);
            farmHarvestDurationSeconds = ObjDouble(t, "farmHarvestDurationSeconds", farmHarvestDurationSeconds);

            treeChopYieldWood = ObjInt(t, "treeChopYieldWood", treeChopYieldWood);
            treeSpreadAttemptsPerSecond = ObjDouble(t, "treeSpreadAttemptsPerSecond", treeSpreadAttemptsPerSecond);
            treeSpreadChancePerAttempt = ObjDouble(t, "treeSpreadChancePerAttempt", treeSpreadChancePerAttempt);

            // Legacy (v4 and earlier): farms produced food continuously via farmFoodPerSecond.
            // Map that into a harvest yield that approximates the same long-term rate.
            if (!t.contains("farmHarvestYieldFood") && t.contains("farmFoodPerSecond"))
            {
                const double oldRate = ObjDouble(t, "farmFoodPerSecond", 0.25);
                farmHarvestYieldFood = oldRate * std::max(1.0, farmGrowDurationSeconds);
            }
            foodPerColonistPerSecond = ObjDouble(t, "foodPerColonistPerSecond", foodPerColonistPerSecond);

            // v3+ hunger/eating tuning (optional; missing fields are fine for old saves).
            colonistMaxPersonalFood    = ObjDouble(t, "colonistMaxPersonalFood", colonistMaxPersonalFood);
            colonistEatThresholdFood   = ObjDouble(t, "colonistEatThresholdFood", colonistEatThresholdFood);
            colonistEatDurationSeconds = ObjDouble(t, "colonistEatDurationSeconds", colonistEatDurationSeconds);

            // v8+ hauling tuning (optional; missing fields are fine for old saves).
            haulCarryCapacity = ObjInt(t, "haulCarryCapacity", haulCarryCapacity);
            haulPickupDurationSeconds = ObjDouble(t, "haulPickupDurationSeconds", haulPickupDurationSeconds);
            haulDropoffDurationSeconds = ObjDouble(t, "haulDropoffDurationSeconds", haulDropoffDurationSeconds);

            // v11+ pathfinding tuning (optional; missing fields are fine for old saves).
            pathAlgo = PathAlgoFromName(ObjString(t, "pathfindingAlgorithm", std::string{PathAlgoName(pathAlgo)}));
            pathCacheEnabled = ObjBool(t, "pathCacheEnabled", pathCacheEnabled);
            pathCacheMaxEntries = ObjInt(t, "pathCacheMaxEntries", pathCacheMaxEntries);
            navUseTerrainCosts = ObjBool(t, "navTerrainCostsEnabled", navUseTerrainCosts);

            // Sanitize.
            colonistMaxPersonalFood    = std::max(0.0, colonistMaxPersonalFood);
            treeChopYieldWood          = std::max(0, treeChopYieldWood);
            treeSpreadAttemptsPerSecond = std::max(0.0, treeSpreadAttemptsPerSecond);
            treeSpreadChancePerAttempt  = std::clamp(treeSpreadChancePerAttempt, 0.0, 1.0);

            colonistEatThresholdFood   = std::clamp(colonistEatThresholdFood, 0.0, colonistMaxPersonalFood);
            colonistEatDurationSeconds = std::max(0.0, colonistEatDurationSeconds);

            haulCarryCapacity = std::max(0, haulCarryCapacity);
            haulPickupDurationSeconds = std::max(0.0, haulPickupDurationSeconds);
            haulDropoffDurationSeconds = std::max(0.0, haulDropoffDurationSeconds);

            pathCacheMaxEntries = std::clamp(pathCacheMaxEntries, 0, 16384);
            if (pathCacheMaxEntries == 0)
                pathCacheEnabled = false;
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
                    const TileType seededBuilt = c.built;
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

                    c.built = BuiltTileFromInt(ArrInt(entry, 0, 0));
                    c.planned = PlanTileFromInt(ArrInt(entry, 1, 0));

                    c.workRemaining = ArrFloat(entry, 2, 0.0f);

                    // v2+ includes planPriority (0..3). Default to 0 for v1.
                    int pr = ArrInt(entry, 3, 0);
                    pr = std::max(0, std::min(3, pr));
                    c.planPriority = static_cast<std::uint8_t>(pr);

                    // v4+ includes builtFromPlan (whether the built tile was produced by completing a plan).
                    if (version >= 4)
                    {
                        c.builtFromPlan = ArrBool(entry, 4, false);
                    }
                    else
                    {
                        // Best-effort inference for legacy saves:
                        // If a tile matches the seeded layout produced by reset(), treat it as seeded;
                        // otherwise treat it as player-built.
                        c.builtFromPlan = (c.built != TileType::Empty && c.built != seededBuilt);
                    }

                    if (c.built == TileType::Empty)
                        c.builtFromPlan = false;

                    // Farm growth (v5+).
                    //
                    // NOTE: One intermediate patch accidentally wrote farmGrowth into a v4 payload
                    // (cells[5]) while still stamping version=4. Accept that shape so players
                    // don't lose progress when loading those saves.
                    const bool hasFarmGrowth = (version >= 5) || (version == 4 && entry.size() >= 6);
                    if (hasFarmGrowth)
                    {
                        c.farmGrowth = clampf(ArrFloat(entry, 5, 0.0f), 0.0f, 1.0f);
                    }
                    else
                    {
                        // For legacy saves, start existing farms as harvestable so a loaded colony
                        // does not immediately starve due to the farming model change.
                        c.farmGrowth = (c.built == TileType::Farm) ? 1.0f : 0.0f;
                    }
                    // Loose wood stacks (v8+). We accept the field whenever present to be robust.
                    if (entry.size() >= 7)
                        c.looseWood = std::max(0, ArrInt(entry, 6, 0));
                    else
                        c.looseWood = 0;

                    c.looseWoodReservedBy = -1;

                    // Reservations are runtime-only.
                    c.farmReservedBy = -1;

                    // Sanitize: never keep a deconstruction plan on an empty tile.
                    if (c.built == TileType::Empty && c.planned == TileType::Remove)
                    {
                        c.planned = TileType::Empty;
                        c.planPriority = 0;
                        c.workRemaining = 0.0f;
                    }

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

                // Hunger (v3+). For legacy saves, start colonists at full.
                const float maxPersonalFood = static_cast<float>(std::max(0.0, colonistMaxPersonalFood));
                if (version >= 3)
                {
                    const float pf = static_cast<float>(ObjDouble(item, "personalFood", maxPersonalFood));
                    c.personalFood = clampf(pf, 0.0f, maxPersonalFood);
                }
                else
                {
                    c.personalFood = maxPersonalFood;
                }

                // Drafted (player manual control). Optional field.
                c.drafted = ObjBool(item, "drafted", false);

                // Roles/progression (v7+; optional fields so old saves still load).
                // Store by name so RoleId reordering is safe.
                {
                    const std::string roleName = ObjString(item, "role", std::string{});
                    const RoleId rid = roleName.empty() ? RoleId::Worker : RoleFromName(roleName);
                    c.role.set(rid);

                    int lvl = ObjInt(item, "roleLevel", 1);
                    lvl = std::max(1, lvl);
                    if (lvl > std::numeric_limits<std::uint16_t>::max())
                        lvl = static_cast<int>(std::numeric_limits<std::uint16_t>::max());

                    // XP is stored as the "remainder" toward the next level.
                    std::uint32_t xp = 0u;
                    if (item.contains("roleXp"))
                    {
                        const int xi = ObjInt(item, "roleXp", 0);
                        xp = static_cast<std::uint32_t>(std::max(0, xi));
                    }

                    c.role.level = static_cast<std::uint16_t>(lvl);
                    c.role.xp = xp;

                    // Sanitize: normalize large XP into levels.
                    while (c.role.xp >= RoleComponent::kXpPerLevel && c.role.level < std::numeric_limits<std::uint16_t>::max())
                    {
                        c.role.xp -= RoleComponent::kXpPerLevel;
                        ++c.role.level;
                    }
                }

                // Work priorities (v9+). Optional field so older saves still load.
                // Priorities are clamped into the supported range [0..4].
                if (version >= 9 && item.contains("workPriorities") && item["workPriorities"].is_object())
                {
                    const json& wp = item["workPriorities"];

                    WorkPriorities p = DefaultWorkPriorities(c.role.role);
                    p.build = static_cast<std::uint8_t>(std::clamp(ObjInt(wp, "build", static_cast<int>(p.build)), 0, 4));
                    p.farm  = static_cast<std::uint8_t>(std::clamp(ObjInt(wp, "farm",  static_cast<int>(p.farm)),  0, 4));
                    p.haul  = static_cast<std::uint8_t>(std::clamp(ObjInt(wp, "haul",  static_cast<int>(p.haul)),  0, 4));

                    c.workPrio = ClampWorkPriorities(p);
                }
                else
                {
                    c.workPrio = DefaultWorkPriorities(c.role.role);
                }

                // Manual order queue (v12+). Optional field so older saves still load.
                c.manualQueue.clear();
                if (item.contains("manualQueue") && item["manualQueue"].is_array())
                {
                    const json& mq = item["manualQueue"];
                    for (const auto& q : mq)
                    {
                        if (!q.is_array() || q.size() < 3)
                            continue;

                        const int kindI = ArrInt(q, 0, 0);
                        const int x = ArrInt(q, 1, 0);
                        const int y = ArrInt(q, 2, 0);

                        Colonist::ManualOrder o;
                        o.kind = static_cast<Colonist::ManualOrder::Kind>(std::clamp(kindI, 0, 2));
                        o.x = x;
                        o.y = y;
                        c.manualQueue.push_back(o);
                    }
                }

                // Clear job/path (reassigned on next tick).
                c.jobKind = Colonist::JobKind::None;
                c.hasJob = false;
                c.targetX = 0;
                c.targetY = 0;
                c.path.clear();
                c.pathIndex = 0;
                c.eatWorkRemaining = 0.0f;
                c.harvestWorkRemaining = 0.0f;

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
        rebuildFarmCache();
        rebuildLooseWoodCache();

        // Recompute derived room/indoors cache.
        rebuildRooms();

        // Allow job assignment immediately after a load.
        m_jobAssignCooldown = 0.0;
        m_harvestAssignCooldown = 0.0;
        m_haulAssignCooldown = 0.0;

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
