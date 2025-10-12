#include <gtest/gtest.h>
#include "colony/pathfinding/AStar.hpp"

using namespace colony::pf;

TEST(AStar, StraightLine) {
    GridMap m(32, 8);
    for (int y=0;y<8;++y) for (int x=0;x<32;++x) m.set_walkable(x,y,1);
    AStar astar(m);
    auto path = astar.find_path({0,4},{31,4});
    ASSERT_FALSE(path.empty());
    // 31 cardinal steps expected
    EXPECT_EQ(path.length(), 32u);
}

TEST(AStar, Blocked) {
    GridMap m(8, 8);
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) m.set_walkable(x,y,1);
    for (int y=0;y<8;++y) m.set_walkable(4,y,0);
    AStar astar(m);
    auto path = astar.find_path({1,1},{6,1});
    EXPECT_TRUE(path.empty());
}

TEST(AStar, DiagonalNoCornerCutting) {
    GridMap m(4, 4);
    for (int y=0;y<4;++y) for (int x=0;x<4;++x) m.set_walkable(x,y,1);
    m.set_walkable(1,0,0); // block top neighbor
    m.set_walkable(0,1,0); // block left neighbor
    AStar astar(m);
    // (0,0)->(1,1) should be disallowed (corner-cut)
    auto path = astar.find_path({0,0},{1,1});
    EXPECT_TRUE(path.empty());
}
