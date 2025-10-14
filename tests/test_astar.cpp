#include <doctest/doctest.h>
#include "colony/pathfinding/AStar.hpp"

using namespace colony::pf;

TEST_CASE("AStar/StraightLine") {
    GridMap m(32, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            m.set_walkable(x, y, 1);

    AStar astar(m);
    auto path = astar.find_path({0,4}, {31,4});

    REQUIRE_FALSE(path.empty());
    CHECK(path.length() == 32u); // points, including start
}

TEST_CASE("AStar/Blocked") {
    GridMap m(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            m.set_walkable(x, y, 1);
    for (int y = 0; y < 8; ++y)
        m.set_walkable(4, y, 0);

    AStar astar(m);
    auto path = astar.find_path({1,1}, {6,1});
    CHECK(path.empty());
}

TEST_CASE("AStar/DiagonalNoCornerCutting") {
    GridMap m(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            m.set_walkable(x, y, 1);
    m.set_walkable(1,0,0);
    m.set_walkable(0,1,0);

    AStar astar(m);
    auto path = astar.find_path({0,0}, {1,1});
    CHECK(path.empty());
}
