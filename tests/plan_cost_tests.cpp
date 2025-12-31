#include <doctest/doctest.h>

#include "game/proto/ProtoWorld.h"

using namespace colony::proto;

TEST_CASE("PlanDeltaWoodCost: basic placement and clearing")
{
    Cell c{};
    c.built = TileType::Empty;
    c.planned = TileType::Empty;
    c.planPriority = 0;

    CHECK(TileWoodCost(TileType::Floor) == 1);
    CHECK(TileWoodCost(TileType::Wall) == 2);

    // Placing a new plan costs wood.
    CHECK(PlanDeltaWoodCost(c, TileType::Floor) == 1);

    // After a floor plan exists, switching to wall only costs the delta.
    c.planned = TileType::Floor;
    CHECK(PlanDeltaWoodCost(c, TileType::Wall) == 1);

    // Clearing refunds the planned material cost.
    CHECK(PlanDeltaWoodCost(c, TileType::Empty) == -TileWoodCost(TileType::Floor));
}

TEST_CASE("PlanDeltaWoodCost: remove special-case")
{
    Cell c{};
    c.built = TileType::Empty;
    c.planned = TileType::Wall;

    // 'Remove' on an empty built tile behaves like clearing a plan.
    CHECK(PlanDeltaWoodCost(c, TileType::Remove) == -TileWoodCost(TileType::Wall));
}

TEST_CASE("PlanWouldChange: matches placePlan change conditions")
{
    Cell c{};
    c.built = TileType::Wall;
    c.planned = TileType::Empty;
    c.planPriority = 0;

    // Planning the already-built tile is a no-op.
    CHECK_FALSE(PlanWouldChange(c, TileType::Wall, 0));

    // Demolishing a built wall should change the cell.
    CHECK(PlanWouldChange(c, TileType::Remove, 0));

    // Priority-only changes only apply to active plans.
    c.built = TileType::Empty;
    c.planned = TileType::Floor;
    c.planPriority = 0;

    CHECK_FALSE(PlanWouldChange(c, TileType::Floor, 0));
    CHECK(PlanWouldChange(c, TileType::Floor, 1));
}
