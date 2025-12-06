// tests/test_jps.cpp  (GoogleTest)
#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "pathfinding/Jps.hpp"

using colony::path::Cell;
using colony::path::JpsOptions;
using colony::path::jps_find_path;
using colony::path::IGrid;

// Minimal concrete grid for tests
class TestGrid final : public IGrid {
public:
    TestGrid(int w, int h) : W(w), H(h), blocked(static_cast<size_t>(w*h), 0) {}
    void setBlocked(int x, int y, bool b = true) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        blocked[static_cast<size_t>(y*W + x)] = b ? 1u : 0u;
    }

    // IGrid
    int  width()  const override { return W; }
    int  height() const override { return H; }
    bool walkable(int x, int y) const override {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return blocked[static_cast<size_t>(y*W + x)] == 0u;
    }

private:
    int W, H;
    std::vector<uint8_t> blocked; // 0 = free, 1 = blocked
};

static JpsOptions DefaultOpts() {
    JpsOptions o{};
    // keep defaults sensible for tests:
    // o.allowDiagonal = true;            // (typical default)
    // o.dontCrossCorners = true;         // checked explicitly in tests below
    // o.heuristicWeight = 1.0f;
    // o.tieBreakCross = true;
    // o.smoothPath = false;              // keep off for deterministic endpoints
    return o;
}

TEST(Jps, StartEqualsGoal) {
    TestGrid g(5, 5);
    JpsOptions o = DefaultOpts();
    Cell s{2,2}, t{2,2};

    auto path = jps_find_path(g, s, t, o);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_EQ(path.front().x, 2);
    EXPECT_EQ(path.front().y, 2);
}

TEST(Jps, BlockedStartOrGoal) {
    TestGrid g(5, 5);
    g.setBlocked(1,1, true);
    JpsOptions o = DefaultOpts();

    // blocked start
    auto p1 = jps_find_path(g, Cell{1,1}, Cell{4,4}, o);
    EXPECT_TRUE(p1.empty());

    // blocked goal
    auto p2 = jps_find_path(g, Cell{0,0}, Cell{1,1}, o);
    EXPECT_TRUE(p2.empty());
}

TEST(Jps, CornerCuttingGuard) {
    // 2x2 grid:
    // S = (0,0), G = (1,1), obstacles at (1,0) and (0,1)
    // With dontCrossCorners = true: no path
    // With dontCrossCorners = false: diagonal path exists
    TestGrid g(2, 2);
    g.setBlocked(1,0, true);
    g.setBlocked(0,1, true);

    // Case A: guard ON -> no path
    {
        JpsOptions o = DefaultOpts();
        o.allowDiagonal = true;
        o.dontCrossCorners = true;

        auto path = jps_find_path(g, Cell{0,0}, Cell{1,1}, o);
        EXPECT_TRUE(path.empty());
    }

    // Case B: guard OFF -> path exists
    {
        JpsOptions o = DefaultOpts();
        o.allowDiagonal = true;
        o.dontCrossCorners = false;

        auto path = jps_find_path(g, Cell{0,0}, Cell{1,1}, o);
        ASSERT_FALSE(path.empty());
        EXPECT_EQ(path.front().x, 0);
        EXPECT_EQ(path.front().y, 0);
        EXPECT_EQ(path.back().x, 1);
        EXPECT_EQ(path.back().y, 1);
    }
}

TEST(Jps, OpenGrid_ReachesGoal) {
    TestGrid g(10, 10);
    JpsOptions o = DefaultOpts();
    o.allowDiagonal = true;
    o.dontCrossCorners = true;
    o.smoothPath = false;

    Cell s{0,0}, t{7,5};
    auto path = jps_find_path(g, s, t, o);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front().x, s.x);
    EXPECT_EQ(path.front().y, s.y);
    EXPECT_EQ(path.back().x,  t.x);
    EXPECT_EQ(path.back().y,  t.y);
}
