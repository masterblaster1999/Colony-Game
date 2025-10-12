#include <gtest/gtest.h>
#include "colony/pathfinding/AStar.hpp"
#include "colony/pathfinding/JPS.hpp"
#include <random>

using namespace colony::pf;

static GridMap make_random(int w,int h, double p, uint32_t seed=1234) {
    GridMap m(w,h);
    std::mt19937 rng(seed);
    std::bernoulli_distribution free_prob(1.0 - p);
    for (int y=0;y<h;++y) for (int x=0;x<w;++x)
        m.set_walkable(x,y, free_prob(rng) ? 1 : 0);
    return m;
}

TEST(JPS, MatchesAStarLengthOnSmallRandom) {
    GridMap m = make_random(32,32,0.15,42);
    // ensure start/goal free
    m.set_walkable(1,1,1);
    m.set_walkable(30,30,1);

    AStar astar(m);
    auto p1 = astar.find_path({1,1},{30,30});

    JPS jps(m);
    auto p2 = jps.find_path({1,1},{30,30});

    if (p1.empty()) {
        EXPECT_TRUE(p2.empty());
    } else {
        EXPECT_FALSE(p2.empty());
        EXPECT_EQ(p1.length(), p2.length());
    }
}
