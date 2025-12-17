// tests/test_jps.cpp (doctest)
//
// NOTE:
//   Do NOT define DOCTEST_CONFIG_IMPLEMENT* in this file.
//   tests/test_main.cpp is the only TU that provides doctest implementation + main.

#include <doctest/doctest.h>

#include "pathfinding/Jps.hpp"

#include <cstddef> // std::size_t
#include <vector>

using colony::path::Cell;
using colony::path::JpsOptions;
using colony::path::jps_find_path;
using colony::path::IGrid;

// NOTE:
// This test file can be compiled in a CMake UNITY_BUILD (aka "unity/jumbo build") where multiple
// .cpp files get merged into a single translation unit. Another test file also defines a
// TestGrid type, so we keep our helper type/function in a unique namespace with a unique name
// to avoid redefinition errors.
namespace colony_smoke_jps {

class JpsTestGrid final : public IGrid {
public:
    JpsTestGrid(int w, int h)
        : W(w), H(h), blocked(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0u) {}

    void setBlocked(int x, int y, bool b = true) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        const std::size_t idx =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x);
        blocked[idx] = b ? 1u : 0u;
    }

    // IGrid
    int  width()  const override { return W; }
    int  height() const override { return H; }
    bool walkable(int x, int y) const override {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        const std::size_t idx =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x);
        return blocked[idx] == 0u;
    }

private:
    int W, H;
    std::vector<unsigned> blocked;
};

static JpsOptions DefaultOptsJps() {
    JpsOptions o{};
    // keep defaults; override per-test below as needed
    return o;
}

} // namespace colony_smoke_jps

TEST_CASE("Jps: StartEqualsGoal") {
    colony_smoke_jps::JpsTestGrid g(5, 5);
    auto o = colony_smoke_jps::DefaultOptsJps();
    Cell s{2, 2}, t{2, 2};

    auto path = jps_find_path(g, s, t, o);
    REQUIRE(path.size() == 1u);
    CHECK(path.front().x == 2);
    CHECK(path.front().y == 2);
}

TEST_CASE("Jps: BlockedStartOrGoal") {
    colony_smoke_jps::JpsTestGrid g(5, 5);
    g.setBlocked(1, 1, true);

    auto o = colony_smoke_jps::DefaultOptsJps();

    auto p1 = jps_find_path(g, Cell{1, 1}, Cell{4, 4}, o);
    CHECK(p1.empty());

    auto p2 = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
    CHECK(p2.empty());
}

TEST_CASE("Jps: Corner cutting guard") {
    colony_smoke_jps::JpsTestGrid g(2, 2);
    g.setBlocked(1, 0, true);
    g.setBlocked(0, 1, true);

    SUBCASE("dontCrossCorners = true → no path") {
        auto o = colony_smoke_jps::DefaultOptsJps();
        o.allowDiagonal    = true;
        o.dontCrossCorners = true;

        auto path = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
        CHECK(path.empty());
    }

    SUBCASE("dontCrossCorners = false → diagonal path") {
        auto o = colony_smoke_jps::DefaultOptsJps();
        o.allowDiagonal    = true;
        o.dontCrossCorners = false;

        auto path = jps_find_path(g, Cell{0, 0}, Cell{1, 1}, o);
        REQUIRE_FALSE(path.empty());
        CHECK(path.front().x == 0);
        CHECK(path.front().y == 0);
        CHECK(path.back().x  == 1);
        CHECK(path.back().y  == 1);
    }
}

