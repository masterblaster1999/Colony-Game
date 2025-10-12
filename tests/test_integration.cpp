#include <gtest/gtest.h>
#include "colony/pathfinding/AStar.hpp"

using namespace colony::pf;

TEST(Integration, WeightedTilesAffectPathCost) {
    GridMap m(8,8);
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) { m.set_walkable(x,y,1); m.set_tile_cost(x,y,1.0f); }
    // Middle band is "mud"
    for (int x=0;x<8;++x) m.set_tile_cost(x,4,3.0f);

    AStar astar(m);
    auto path = astar.find_path({0,0},{7,7});
    ASSERT_FALSE(path.empty());
    // Expect path to detour around row y=4 rather than cross it diagonally
    bool crosses_mud = std::any_of(path.points.begin(), path.points.end(), [](const IVec2& p){ return p.y==4; });
    EXPECT_FALSE(crosses_mud);
}
