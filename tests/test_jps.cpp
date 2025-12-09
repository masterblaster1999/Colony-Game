// tests/test_jps.cpp (doctest)
#include <vector>
#include <doctest/doctest.h>
#include "pathfinding/Jps.hpp"

using colony::path::Cell;
using colony::path::JpsOptions;
using colony::path::jps_find_path;
using colony::path::IGrid;

class TestGrid final : public IGrid {
public:
    TestGrid(int w, int h)
        : W(w), H(h), blocked(static_cast<size_t>(w * h), 0u) {}

    void setBlocked(int x, int y, bool b = true) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        blocked[static_cast<size_t>(y * W + x)] = b ? 1u : 0u;
    }

    // IGrid
    int  width()  const override { return W; }
    int  height() const override { return H; }
    bool walkable(int x, int y) const override {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return blocked[static_cast<size_t>(y * W + x)] == 0u;
    }

private:
    int W, H;
    std::vector<unsigned> blocked;
};

static JpsOptions DefaultOpts() {
    JpsOptions o{};
    // keep defaults; override per-test below as needed
    return o;
}

TEST_CASE("Jps: StartEqualsGoal") {
    TestGrid g(5, 5);
    auto o = DefaultOpts();
    Cell s{2, 2}, t{2, 2};

    auto path = jps_find_path(g, s, t, o);
    REQUIRE(path.size() == 1u);
    CHECK(path.front().x == 2);
    CHECK(path.front().y == 2);
}

TEST_CASE("Jps: BlockedStartOrGoal") {
    TestGrid g(5, 5);
    g.setBlocked(1, 1, true);

    auto o = DefaultOpts();

    auto p1 = jps_find_path(g, Cell{1, 1}, Cell{4, 4}, o);
    CHECK(p1.empty());

    auto p2 = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
    CHECK(p2.empty());
}

TEST_CASE("Jps: Corner cutting guard") {
    TestGrid g(2, 2);
    g.setBlocked(1, 0, true);
    g.setBlocked(0, 1, true);

    SUBCASE("dontCrossCorners = true → no path") {
        auto o = DefaultOpts();
        o.allowDiagonal   = true;
        o.dontCrossCorners = true;

        auto path = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
        CHECK(path.empty());
    }

    SUBCASE("dontCrossCorners = false → diagonal path") {
        auto o = DefaultOpts();
        o.allowDiagonal   = true;
        o.dontCrossCorners = false;

        auto path = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
        REQUIRE_FALSE(path.empty());
        CHECK(path.front().x == 0);
        CHECK(path.front().y == 0);
        CHECK(path.back().x  == 1);
        CHECK(path.back().y  == 1);
    }
}
