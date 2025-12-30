#include "doctest/doctest.h"

#include "game/proto/ProtoWorld.h"
#include "game/proto/ProtoWorld_SaveFormat.h"

#include "game/Role.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {
namespace fs = std::filesystem;

fs::path MakeUniqueTempPath(const char* stem, const char* ext)
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path();
    p /= std::string(stem) + "_" + std::to_string(static_cast<long long>(now)) + ext;
    return p;
}

bool ReadFileToString(const fs::path& p, std::string& out)
{
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz < 0)
        return false;
    f.seekg(0, std::ios::beg);

    out.resize(static_cast<std::size_t>(sz));
    f.read(out.data(), static_cast<std::streamsize>(sz));
    return (f.gcount() == static_cast<std::streamsize>(sz));
}

bool WriteStringToFile(const fs::path& p, const std::string& bytes)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

void RemoveQuiet(const fs::path& p) noexcept
{
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

TEST_CASE("ProtoWorld SaveJson format/version and legacy format compatibility")
{
    using colony::proto::World;
    using colony::proto::savefmt::kWorldFormat;
    using colony::proto::savefmt::kWorldFormatLegacy;
    using colony::proto::savefmt::kWorldVersion;

    const fs::path pNew = MakeUniqueTempPath("colony_proto_world", ".json");
    const fs::path pOld = MakeUniqueTempPath("colony_proto_world_legacy", ".json");

    struct Cleanup {
        fs::path a;
        fs::path b;
        ~Cleanup() { RemoveQuiet(a); RemoveQuiet(b); }
    } cleanup{pNew, pOld};

    World w(8, 6, /*seed*/ 123);
    w.inventory().wood = 42;
    w.inventory().food = 13.5f;

    // Verify that player-control state persists (drafted colonists).
    if (!w.colonists().empty())
    {
        w.colonists()[0].drafted = true;

        // v7+: roles + progression
        w.colonists()[0].role.set(RoleId::Builder);
        w.colonists()[0].role.level = 2;
        w.colonists()[0].role.xp    = 123;

        // v9+: work priorities
        w.colonists()[0].workPrio.build = 1;
        w.colonists()[0].workPrio.farm = 0;
        w.colonists()[0].workPrio.haul = 3;
    }

    // v11: Pathfinding tuning (ensure these round-trip and appear in the JSON).
    w.pathAlgo = colony::proto::PathAlgo::JumpPointSearch;
    w.pathCacheEnabled = false;
    w.pathCacheMaxEntries = 123;
    w.navUseTerrainCosts = false;

    // A tiny bit of state so the file isn't totally trivial.
    (void)w.placePlan(1, 1, colony::proto::TileType::Wall, /*priority*/ 2);

    // Mark a tile as player-built to verify v4 cell field round-trips.
    w.cell(2, 2).builtFromPlan = true;

    // Set up a farm tile with a non-trivial growth value to verify v5 persistence.
    w.cell(3, 2).built = colony::proto::TileType::Farm;
    w.cell(3, 2).farmGrowth = 0.75f;

    // Set up a tree tile to verify v6 built tile round-trips.
    w.cell(1, 2).built = colony::proto::TileType::Tree;
    w.cell(1, 2).builtFromPlan = false;

    // v8: loose wood piles (hauled to stockpiles)
    w.cell(4, 2).looseWood = 7;

    std::string err;
    REQUIRE_MESSAGE(w.SaveJson(pNew, &err), err);

    std::string bytes;
    REQUIRE(ReadFileToString(pNew, bytes));

    const nlohmann::json j = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    REQUIRE(j.is_object());

    CHECK(j.value("format", "") == std::string(kWorldFormat));
    CHECK(j.value("version", 0) == kWorldVersion);

    // v3+ fields: hunger tuning + per-colonist personalFood.
    REQUIRE(j.contains("tuning"));
    REQUIRE(j["tuning"].is_object());
    CHECK(j["tuning"].contains("colonistMaxPersonalFood"));
    CHECK(j["tuning"].contains("colonistEatThresholdFood"));
    CHECK(j["tuning"].contains("colonistEatDurationSeconds"));
    CHECK(j["tuning"].contains("farmGrowDurationSeconds"));
    CHECK(j["tuning"].contains("farmHarvestYieldFood"));
    CHECK(j["tuning"].contains("farmHarvestDurationSeconds"));
    CHECK(j["tuning"].contains("treeChopYieldWood"));
    CHECK(j["tuning"].contains("treeSpreadAttemptsPerSecond"));
    CHECK(j["tuning"].contains("treeSpreadChancePerAttempt"));
    CHECK(j["tuning"].contains("haulCarryCapacity"));
    CHECK(j["tuning"].contains("haulPickupDurationSeconds"));
    CHECK(j["tuning"].contains("haulDropoffDurationSeconds"));

    // v11+ fields: pathfinding tuning
    CHECK(j["tuning"].contains("pathfindingAlgorithm"));
    CHECK(j["tuning"].contains("pathCacheEnabled"));
    CHECK(j["tuning"].contains("pathCacheMaxEntries"));
    CHECK(j["tuning"].contains("navTerrainCostsEnabled"));

    CHECK(j["tuning"]["pathfindingAlgorithm"].get<std::string>() == "JPS");
    CHECK(j["tuning"]["pathCacheEnabled"].get<bool>() == false);
    CHECK(j["tuning"]["pathCacheMaxEntries"].get<int>() == 123);
    CHECK(j["tuning"]["navTerrainCostsEnabled"].get<bool>() == false);

    REQUIRE(j.contains("colonists"));
    REQUIRE(j["colonists"].is_array());
    if (!j["colonists"].empty())
    {
        const auto& c0 = j["colonists"][0];
        REQUIRE(c0.is_object());
        REQUIRE(c0.contains("personalFood"));
        CHECK(c0["personalFood"].is_number());

        REQUIRE(c0.contains("drafted"));
        CHECK(c0["drafted"].is_boolean());
        CHECK(c0["drafted"].get<bool>() == true);

        // v7+: roles + progression
        REQUIRE(c0.contains("role"));
        CHECK(c0["role"].is_string());
        CHECK(c0["role"].get<std::string>() == std::string(RoleDefOf(RoleId::Builder).name));

        REQUIRE(c0.contains("roleLevel"));
        CHECK(c0["roleLevel"].is_number_integer());
        CHECK(c0["roleLevel"].get<int>() == 2);

        REQUIRE(c0.contains("roleXp"));
        CHECK(c0["roleXp"].is_number());
        CHECK(c0["roleXp"].get<int>() == 123);


        // v9+: work priorities
        REQUIRE(c0.contains("workPriorities"));
        REQUIRE(c0["workPriorities"].is_object());
        CHECK(c0["workPriorities"]["build"].get<int>() == 1);
        CHECK(c0["workPriorities"]["farm"].get<int>() == 0);
        CHECK(c0["workPriorities"]["haul"].get<int>() == 3);
    }

    // v5+ cells include farmGrowth as the 6th array element (and keep builtFromPlan at index 4).
    REQUIRE(j.contains("cells"));
    REQUIRE(j["cells"].is_array());
    if (!j["cells"].empty())
    {
        const auto& e0 = j["cells"][0];
        REQUIRE(e0.is_array());
        CHECK(e0.size() >= 7);
    }

    {
        const int fx = 3;
        const int fy = 2;
        const std::size_t idx = static_cast<std::size_t>(fy * w.width() + fx);
        REQUIRE(idx < j["cells"].size());

        const auto& ef = j["cells"][idx];
        REQUIRE(ef.is_array());
        REQUIRE(ef.size() >= 7);
        CHECK(ef[0].get<int>() == static_cast<int>(colony::proto::TileType::Farm));
        CHECK(ef[5].get<float>() == doctest::Approx(0.75f));
    }

    {
        const int tx = 1;
        const int ty = 2;
        const std::size_t idx = static_cast<std::size_t>(ty * w.width() + tx);
        REQUIRE(idx < j["cells"].size());

        const auto& et = j["cells"][idx];
        REQUIRE(et.is_array());
        REQUIRE(et.size() >= 7);
        CHECK(et[0].get<int>() == static_cast<int>(colony::proto::TileType::Tree));
    }

    {
        const int lx = 4;
        const int ly = 2;
        const std::size_t idx = static_cast<std::size_t>(ly * w.width() + lx);
        REQUIRE(idx < j["cells"].size());

        const auto& lw = j["cells"][idx];
        REQUIRE(lw.is_array());
        REQUIRE(lw.size() >= 7);
        CHECK(lw[6].get<int>() == 7);
    }

    // Rewrite the same payload with the legacy format string.
    nlohmann::json jLegacy = j;
    jLegacy["format"] = kWorldFormatLegacy;

    REQUIRE(WriteStringToFile(pOld, jLegacy.dump(2)));

    World loaded;
    REQUIRE_MESSAGE(loaded.LoadJson(pOld, &err), err);

    CHECK(loaded.width() == w.width());
    CHECK(loaded.height() == w.height());
    CHECK(loaded.inventory().wood == w.inventory().wood);

    // v11: pathfinding tuning should round-trip.
    CHECK(loaded.pathAlgo == w.pathAlgo);
    CHECK(loaded.pathCacheEnabled == w.pathCacheEnabled);
    CHECK(loaded.pathCacheMaxEntries == w.pathCacheMaxEntries);
    CHECK(loaded.navUseTerrainCosts == w.navUseTerrainCosts);


    CHECK(loaded.cell(2, 2).builtFromPlan == w.cell(2, 2).builtFromPlan);

    CHECK(loaded.cell(3, 2).built == colony::proto::TileType::Farm);
    CHECK(loaded.cell(3, 2).farmGrowth == doctest::Approx(0.75f));

    CHECK(loaded.cell(1, 2).built == colony::proto::TileType::Tree);

    CHECK(loaded.cell(4, 2).looseWood == 7);

    REQUIRE(loaded.colonists().size() == w.colonists().size());
    if (!loaded.colonists().empty())
    {
        CHECK(loaded.colonists()[0].personalFood == doctest::Approx(w.colonists()[0].personalFood));
        CHECK(loaded.colonists()[0].drafted == w.colonists()[0].drafted);

        CHECK(loaded.colonists()[0].role.role == w.colonists()[0].role.role);
        CHECK(loaded.colonists()[0].role.level == w.colonists()[0].role.level);
        CHECK(loaded.colonists()[0].role.xp == w.colonists()[0].role.xp);


        CHECK(loaded.colonists()[0].workPrio.build == w.colonists()[0].workPrio.build);
        CHECK(loaded.colonists()[0].workPrio.farm == w.colonists()[0].workPrio.farm);
        CHECK(loaded.colonists()[0].workPrio.haul == w.colonists()[0].workPrio.haul);
    }

}
