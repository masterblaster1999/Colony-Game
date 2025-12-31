#include <doctest/doctest.h>

#include "game/editor/Blueprint.h"

#include <string>

using namespace colony::game::editor;
using colony::proto::TileType;

TEST_CASE("Blueprint JSON roundtrip (RLE)")
{
    PlanBlueprint bp;
    bp.w = 3;
    bp.h = 2;
    bp.packed = {
        BlueprintPack(TileType::Floor, 0),
        BlueprintPack(TileType::Wall, 1),
        BlueprintPack(TileType::Empty, 0),
        BlueprintPack(TileType::Door, 2),
        BlueprintPack(TileType::Remove, 3),
        BlueprintPack(TileType::Floor, 0),
    };

    const std::string json = PlanBlueprintToJson(bp);
    REQUIRE_FALSE(json.empty());

    PlanBlueprint out;
    std::string err;
    REQUIRE(PlanBlueprintFromJson(json, out, &err));
    CHECK(err.empty());
    CHECK(out.w == bp.w);
    CHECK(out.h == bp.h);
    CHECK(out.packed == bp.packed);
}

TEST_CASE("Blueprint legacy 'cells' array is still accepted")
{
    const int a = static_cast<int>(BlueprintPack(TileType::Floor, 0));
    const int b = static_cast<int>(BlueprintPack(TileType::Wall, 3));

    const std::string json =
        std::string("{\"type\":\"colony_plan_blueprint\",\"version\":1,\"w\":2,\"h\":2,\"cells\":[") +
        std::to_string(a) + "," +
        std::to_string(0) + "," +
        std::to_string(0) + "," +
        std::to_string(b) + "]}";

    PlanBlueprint out;
    std::string err;
    REQUIRE(PlanBlueprintFromJson(json, out, &err));
    CHECK(out.w == 2);
    CHECK(out.h == 2);
    REQUIRE(out.packed.size() == 4);

    CHECK(BlueprintUnpackTile(out.packed[0]) == TileType::Floor);
    CHECK(BlueprintUnpackTile(out.packed[1]) == TileType::Empty);
    CHECK(BlueprintUnpackTile(out.packed[2]) == TileType::Empty);
    CHECK(BlueprintUnpackTile(out.packed[3]) == TileType::Wall);

    CHECK(BlueprintUnpackPriority(out.packed[3]) == 3);
}

TEST_CASE("Blueprint type mismatch is rejected")
{
    const std::string json =
        "{\"type\":\"not_a_blueprint\",\"version\":1,\"w\":1,\"h\":1,\"rle\":[[1,1]]}";

    PlanBlueprint out;
    std::string err;
    CHECK_FALSE(PlanBlueprintFromJson(json, out, &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Blueprint trim removes empty borders")
{
    PlanBlueprint bp;
    bp.w = 4;
    bp.h = 4;
    bp.packed.assign(16, BlueprintPack(TileType::Empty, 0));

    // Place a 2x2 block in the middle: (1,1)..(2,2)
    bp.packed[1 + 1 * 4] = BlueprintPack(TileType::Floor, 0);
    bp.packed[2 + 1 * 4] = BlueprintPack(TileType::Wall,  0);
    bp.packed[1 + 2 * 4] = BlueprintPack(TileType::Door,  2);
    bp.packed[2 + 2 * 4] = BlueprintPack(TileType::Remove, 3);

    const BlueprintBounds b = BlueprintNonEmptyBounds(bp);
    CHECK(b.Empty() == false);
    CHECK(b.x0 == 1);
    CHECK(b.y0 == 1);
    CHECK(b.x1 == 2);
    CHECK(b.y1 == 2);

    const PlanBlueprint trimmed = BlueprintTrimEmptyBorders(bp);
    CHECK(trimmed.w == 2);
    CHECK(trimmed.h == 2);
    REQUIRE(trimmed.packed.size() == 4);

    CHECK(BlueprintUnpackTile(trimmed.packed[0]) == TileType::Floor);
    CHECK(BlueprintUnpackTile(trimmed.packed[1]) == TileType::Wall);
    CHECK(BlueprintUnpackTile(trimmed.packed[2]) == TileType::Door);
    CHECK(BlueprintUnpackTile(trimmed.packed[3]) == TileType::Remove);
    CHECK(BlueprintUnpackPriority(trimmed.packed[2]) == 2);
    CHECK(BlueprintUnpackPriority(trimmed.packed[3]) == 3);
}

TEST_CASE("Blueprint hash is stable and changes with content")
{
    PlanBlueprint a;
    a.w = 2;
    a.h = 1;
    a.packed = {
        BlueprintPack(TileType::Floor, 0),
        BlueprintPack(TileType::Wall, 1),
    };

    PlanBlueprint b = a;

    const std::uint64_t ha = BlueprintHash64(a);
    const std::uint64_t hb = BlueprintHash64(b);
    CHECK(ha == hb);

    // Mutate one cell.
    b.packed[1] = BlueprintPack(TileType::Wall, 2);
    const std::uint64_t hc = BlueprintHash64(b);
    CHECK(ha != hc);
}
