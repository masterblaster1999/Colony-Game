#include "doctest/doctest.h"

#include "game/proto/ProtoWorld.h"

// Round 43 introduces shared, multi-source distance fields for Eat and Harvest
// job assignment. These tests validate that the fields are actually used (via
// PathfindStats) and that the resulting jobs target the expected tiles.

using colony::proto::Colonist;
using colony::proto::PlacePlanResult;
using colony::proto::TileType;
using colony::proto::World;

TEST_CASE("Eat job assignment uses cached food distance field")
{
    World w(7, 7, 1);

    // Keep deterministic and small.
    auto& cs = w.colonists();
    REQUIRE(cs.size() >= 1);
    cs.resize(1);

    // Ensure we are standing on a normal floor tile adjacent to the default stockpile.
    // The reset() stockpile is centered at (w/2,h/2) => (3,3) for a 7x7 world.
    cs[0].x = 2.5f;
    cs[0].y = 3.5f;
    cs[0].hasJob = false;
    cs[0].drafted = false;

    // Make colonist hungry.
    w.colonistMaxPersonalFood = 10.0;
    w.colonistEatThresholdFood = 5.0;
    cs[0].personalFood = 0.0f;

    // One tick should assign an Eat job.
    w.tick(0.1);

    CHECK(cs[0].hasJob);
    CHECK(cs[0].jobKind == Colonist::JobKind::Eat);

    // Target should be the stockpile tile.
    CHECK(w.cell(cs[0].targetX, cs[0].targetY).built == TileType::Stockpile);

    const auto stats = w.pathStats();
    CHECK(stats.eatFieldComputed >= 1);
    CHECK(stats.eatFieldAssigned >= 1);
}

TEST_CASE("Harvest job assignment uses shared harvest distance field")
{
    World w(9, 9, 1);

    auto& cs = w.colonists();
    REQUIRE(cs.size() >= 1);
    cs.resize(1);

    cs[0].x = 4.5f;
    cs[0].y = 4.5f;
    cs[0].hasJob = false;
    cs[0].drafted = false;

    // Make sure hunger doesn't block harvest.
    cs[0].personalFood = 10.0f;
    w.colonistEatThresholdFood = 1.0;

    // Speed up building and make farms instantly grow to harvestable.
    w.buildWorkPerSecond = 100.0;
    w.farmGrowDurationSeconds = 0.0;
    w.farmHarvestDurationSeconds = 10.0; // keep harvest in-progress so the job remains after the tick

    // Place a farm plan near the center start area.
    const int farmX = 6;
    const int farmY = 4;
    REQUIRE(w.placePlan(farmX, farmY, TileType::Farm, /*priority=*/0) == PlacePlanResult::Ok);

    // Tick once: builder should complete the farm construction this tick.
    w.tick(0.25);
    CHECK(w.cell(farmX, farmY).built == TileType::Farm);

    // Tick again: growth step runs first and makes it harvestable; then assignment should pick it up.
    w.tick(0.25);

    CHECK(cs[0].hasJob);
    CHECK(cs[0].jobKind == Colonist::JobKind::Harvest);
    CHECK(cs[0].targetX == farmX);
    CHECK(cs[0].targetY == farmY);

    const auto stats = w.pathStats();
    CHECK(stats.harvestFieldComputed >= 1);
    CHECK(stats.harvestFieldAssigned >= 1);
}
