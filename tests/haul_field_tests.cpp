#include "doctest.h"

#include "game/proto/ProtoWorld.h"

TEST_CASE("haul pickup field assigns distinct piles with fallback")
{
    namespace proto = colony::game::proto;

    proto::World w;
    w.reset(5, 5, /*seed=*/1);

    // Speed the simulation up so the test runs quickly.
    w.colonistWalkSpeed = 50.0;
    w.buildWorkPerSecond = 1000.0;
    w.colonistEatThresholdFood = 0.0; // don't skip work due to hunger

    // Two colonists.
    w.colonists().resize(2);
    w.colonists()[0].id = 0;
    w.colonists()[1].id = 1;

    const int cx = 2;
    const int cy = 2;

    // Start both at the center.
    w.colonists()[0].x = static_cast<float>(cx) + 0.5f;
    w.colonists()[0].y = static_cast<float>(cy) + 0.5f;
    w.colonists()[1].x = static_cast<float>(cx) + 0.5f;
    w.colonists()[1].y = static_cast<float>(cy) + 0.5f;

    // Draft them so we can issue manual build orders and prevent autonomous hauling
    // during setup.
    REQUIRE(w.SetColonistDrafted(0, true) == proto::OrderResult::Ok);
    REQUIRE(w.SetColonistDrafted(1, true) == proto::OrderResult::Ok);

    // Create two loose-wood piles by building trees and then chopping them.
    // One pile is near the stockpile (center), the other is far away.
    const int nearX = 3;
    const int nearY = 2;
    const int farX = 0;
    const int farY = 0;

    REQUIRE(w.placePlan(nearX, nearY, proto::TileType::Tree, 0) == proto::PlanResult::Ok);
    REQUIRE(w.placePlan(farX, farY, proto::TileType::Tree, 0) == proto::PlanResult::Ok);

    REQUIRE(w.OrderColonistBuild(0, nearX, nearY, /*queue=*/false) == proto::OrderResult::Ok);
    REQUIRE(w.OrderColonistBuild(1, farX, farY, /*queue=*/false) == proto::OrderResult::Ok);

    // Tick until both trees are built.
    for (int i = 0; i < 30; ++i)
        w.tick(0.1);

    CHECK(w.cell(nearX, nearY).built == proto::TileType::Tree);
    CHECK(w.cell(farX, farY).built == proto::TileType::Tree);

    // Chop them (Remove plans) to drop loose wood.
    REQUIRE(w.placePlan(nearX, nearY, proto::TileType::Remove, 0) == proto::PlanResult::Ok);
    REQUIRE(w.placePlan(farX, farY, proto::TileType::Remove, 0) == proto::PlanResult::Ok);

    REQUIRE(w.OrderColonistBuild(0, nearX, nearY, /*queue=*/false) == proto::OrderResult::Ok);
    REQUIRE(w.OrderColonistBuild(1, farX, farY, /*queue=*/false) == proto::OrderResult::Ok);

    for (int i = 0; i < 30; ++i)
        w.tick(0.1);

    CHECK(w.cell(nearX, nearY).built == proto::TileType::Empty);
    CHECK(w.cell(farX, farY).built == proto::TileType::Empty);

    CHECK(w.cell(nearX, nearY).looseWood > 0);
    CHECK(w.cell(farX, farY).looseWood > 0);

    // Undraft so autonomous hauling can kick in.
    REQUIRE(w.SetColonistDrafted(0, false) == proto::OrderResult::Ok);
    REQUIRE(w.SetColonistDrafted(1, false) == proto::OrderResult::Ok);

    // Clear any lingering jobs/reservations from the setup work.
    w.CancelAllJobsAndClearReservations();

    // Re-center both colonists so they initially want the same "best" pile.
    w.colonists()[0].x = static_cast<float>(cx) + 0.5f;
    w.colonists()[0].y = static_cast<float>(cy) + 0.5f;
    w.colonists()[1].x = static_cast<float>(cx) + 0.5f;
    w.colonists()[1].y = static_cast<float>(cy) + 0.5f;

    const auto before = w.pathStats();

    // One tick is enough to run hauling assignment; use a small dt so nobody can
    // reach the pile and clear the reservation within the same update.
    w.tick(0.05);

    const auto after = w.pathStats();

    CHECK(after.haulPickupFieldComputed > before.haulPickupFieldComputed);
    CHECK(after.haulPickupFieldAssigned > before.haulPickupFieldAssigned);

    const auto& c0 = w.colonists()[0];
    const auto& c1 = w.colonists()[1];

    REQUIRE(c0.hasJob);
    REQUIRE(c1.hasJob);
    CHECK(c0.jobKind == proto::World::Colonist::JobKind::HaulWood);
    CHECK(c1.jobKind == proto::World::Colonist::JobKind::HaulWood);

    // The two colonists should not reserve the same loose-wood tile.
    CHECK((c0.haulPickupX != c1.haulPickupX) || (c0.haulPickupY != c1.haulPickupY));

    CHECK(w.cell(c0.haulPickupX, c0.haulPickupY).looseWoodReservedBy == c0.id);
    CHECK(w.cell(c1.haulPickupX, c1.haulPickupY).looseWoodReservedBy == c1.id);

    // With both colonists starting from the same tile, the second assignment should
    // need a fallback once the first pile is reserved.
    CHECK(after.haulPickupFieldFallback >= before.haulPickupFieldFallback + 1);
}
