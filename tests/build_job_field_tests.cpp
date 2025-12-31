#include "doctest/doctest.h"

#include "game/proto/ProtoWorld.h"

// These tests focus on the build-job assignment pipeline. Round 39 introduces a
// multi-source "nearest plan" distance field that accelerates assignment when
// many builders are idle.

using colony::proto::Cell;
using colony::proto::Colonist;
using colony::proto::PlacePlanResult;
using colony::proto::TileType;
using colony::proto::World;

TEST_CASE("Build job assignment uses a plan distance field and resolves conflicts via fallback")
{
    // Small world avoids random rocks/trees (the reset() generator skips the whole map)
    // and produces a predictable all-floor start area.
    World w(7, 7, 1);

    // Keep the test small and deterministic.
    auto& cs = w.colonists();
    REQUIRE(cs.size() >= 2);
    cs.resize(2);

    // Put both colonists on the same tile so they'll initially prefer the same nearest plan.
    cs[0].x = 3.5f;
    cs[0].y = 3.5f;
    cs[1].x = 3.5f;
    cs[1].y = 3.5f;

    cs[0].hasJob = false;
    cs[1].hasJob = false;
    cs[0].drafted = false;
    cs[1].drafted = false;

    // Ensure hunger is not blocking job pickup.
    cs[0].personalFood = 10.0f;
    cs[1].personalFood = 10.0f;
    w.colonistEatThresholdFood = 0.5;

    // Place two build plans: one adjacent (very attractive), one far.
    REQUIRE(w.placePlan(4, 3, TileType::Wall, /*priority=*/0) == PlacePlanResult::Ok);
    REQUIRE(w.placePlan(6, 6, TileType::Wall, /*priority=*/0) == PlacePlanResult::Ok);

    // First tick should assign jobs.
    w.tick(0.25);

    CHECK(cs[0].hasJob);
    CHECK(cs[1].hasJob);
    CHECK(cs[0].jobKind == Colonist::JobKind::BuildPlan);
    CHECK(cs[1].jobKind == Colonist::JobKind::BuildPlan);

    const Cell& a = w.cell(4, 3);
    const Cell& b = w.cell(6, 6);
    CHECK(a.reservedBy != -1);
    CHECK(b.reservedBy != -1);
    CHECK(a.reservedBy != b.reservedBy);

    const auto stats = w.pathStats();
    CHECK(stats.buildFieldComputed >= 1);
    CHECK(stats.buildFieldAssigned >= 1);
    CHECK(stats.buildFieldFallback >= 1);
}
