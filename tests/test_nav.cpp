// tests/test_nav.cpp
#include <vector>
#include <cstdint>
#include <doctest/doctest.h>

#include "nav/IGridMap.h"
#include "nav/Navigator.h"

using namespace colony::nav;

struct TestGrid final : IGridMap {
    int32_t w = 0, h = 0;
    std::vector<std::uint8_t> pass;

    explicit TestGrid(int32_t W, int32_t H)
        : w(W), h(H), pass(static_cast<size_t>(W) * static_cast<size_t>(H), 1) {}

    int32_t Width()  const override { return w; }
    int32_t Height() const override { return h; }

    bool IsPassable(int32_t x, int32_t y) const override {
        return pass[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] != 0;
    }
};

static void Line(TestGrid& g, int x0, int y0, int x1, int y1) {
    int dx = (x1 > x0) - (x1 < x0);
    int dy = (y1 > y0) - (y1 < y0);

    int x = x0, y = y0;
    for (;;) {
        g.pass[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] = 0; // mark as blocked
        if (x == x1 && y == y1) break;
        x += dx;
        y += dy;
    }
}

TEST_CASE("Navigator: simple open grid path") {
    TestGrid g(32, 32);
    Navigator nav(static_cast<const IGridMap&>(g));

    auto p = nav.FindPath({0, 0}, {31, 31});

    REQUIRE(p.has_value());
    CHECK_FALSE(p->points.empty());
    CHECK(p->points.front().x == 0);
    CHECK(p->points.front().y == 0);
    CHECK(p->points.back().x == 31);
    CHECK(p->points.back().y == 31);
}

TEST_CASE("Navigator: wall with gap (no corner cutting)") {
    TestGrid g(32, 32);

    // horizontal wall at y=15, open gap at x=16
    Line(g, 0, 15, 30, 15);
    g.pass[static_cast<size_t>(15) * static_cast<size_t>(g.w) + static_cast<size_t>(16)] = 1; // open the gap

    Navigator nav(static_cast<const IGridMap&>(g));
    auto p = nav.FindPath({4, 10}, {28, 20});

    REQUIRE(p.has_value());

    bool crossedGap = false;
    for (auto& c : p->points) {
        if (c.y == 15 && c.x == 16) {
            crossedGap = true;
            break;
        }
    }
    CHECK(crossedGap);
}

TEST_CASE("Navigator: multi-cluster path between clusters") {
    TestGrid g(96, 96);

    // block everything except a vertical corridor at x=48
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            g.pass[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] =
                (x == 48) ? 1 : 0;
        }
    }

    Navigator::Options opt;
    opt.cluster.clusterW     = 32;
    opt.cluster.clusterH     = 32;
    opt.cluster.portalStride = 8;

    Navigator nav(static_cast<const IGridMap&>(g), opt);
    auto p = nav.FindPath({4, 4}, {90, 90});

    CHECK(p.has_value());
}
