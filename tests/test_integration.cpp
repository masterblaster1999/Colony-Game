// tests/test_integration.cpp
#include "doctest/doctest.h"

#include <algorithm>

#include "colony/pathfinding/AStar.hpp"

using namespace colony::pf;

TEST_CASE("Integration/WeightedTilesAffectPathCost") {
    GridMap m(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            m.set_walkable(x, y, 1);
            m.set_tile_cost(x, y, 1.0f);
        }

    // Put a single "mud" tile directly on the straight route.
    // This should force a detour because step_cost() multiplies by tile_cost(dest).
    m.set_tile_cost(2, 2, 100.0f);

    AStar astar(m);
    auto path = astar.find_path({0, 2}, {4, 2});
    REQUIRE_FALSE(path.empty());

    const bool uses_mud = std::any_of(path.points.begin(), path.points.end(),
        [](const IVec2& p) { return p.x == 2 && p.y == 2; });

    CHECK_FALSE(uses_mud);
}
