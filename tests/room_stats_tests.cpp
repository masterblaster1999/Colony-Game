#include <doctest/doctest.h>

#include "game/proto/ProtoWorld.h"

using colony::game::proto::World;
using colony::proto::TileType;

TEST_CASE("Room stats: perimeter + doors")
{
    World w;
    w.reset(10, 10, 1234);

    // Clear the map to a known baseline.
    for (int y = 0; y < w.height(); ++y)
    {
        for (int x = 0; x < w.width(); ++x)
        {
            REQUIRE(w.DebugSetBuiltTile(x, y, TileType::Empty));
        }
    }

    // Build a closed wall ring around a 4x4 interior region (x/y in [3,6]).
    // Walls occupy the perimeter at x/y in [2,7].
    for (int x = 2; x <= 7; ++x)
    {
        REQUIRE(w.DebugSetBuiltTile(x, 2, TileType::Wall));
        REQUIRE(w.DebugSetBuiltTile(x, 7, TileType::Wall));
    }
    for (int y = 2; y <= 7; ++y)
    {
        REQUIRE(w.DebugSetBuiltTile(2, y, TileType::Wall));
        REQUIRE(w.DebugSetBuiltTile(7, y, TileType::Wall));
    }

    // Swap one wall segment for a door.
    REQUIRE(w.DebugSetBuiltTile(4, 2, TileType::Door));

    // Recompute room caches now that we directly edited the map.
    w.DebugRebuildRoomsNow();

    const int interiorRid = w.roomIdAt(4, 4);
    REQUIRE(interiorRid >= 0);

    const World::RoomInfo* ri = w.roomInfoById(interiorRid);
    REQUIRE(ri);

    CHECK(ri->indoors == true);
    CHECK(ri->area == 4 * 4);
    CHECK(ri->perimeter == 2 * (4 + 4)); // 16 tile-edges
    CHECK(ri->doorCount == 1);
}
