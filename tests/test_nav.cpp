// tests/test_nav.cpp
#include <vector>
#include <cstdint>
#include <doctest/doctest.h>

#include "nav/IGridMap.h"
#include "nav/Navigator.h"

// NOTE (Unity builds):
// CMake unity builds can compile multiple .cpp tests into one translation unit, which can cause
// redefinition errors if helpers share names (e.g., TestGrid in multiple tests).
// To make this file robust even under unity, keep helpers in a unique namespace and use unique names.

namespace colony_smoke_nav_test {

struct NavTestGrid final : colony::nav::IGridMap
{
    int32_t w = 0, h = 0;
    std::vector<std::uint8_t> pass;

    explicit NavTestGrid(int32_t W, int32_t H)
        : w(W), h(H), pass(static_cast<size_t>(W) * static_cast<size_t>(H), 1) {}

    int32_t Width()  const override { return w; }
    int32_t Height() const override { return h; }

    bool IsPassable(int32_t x, int32_t y) const override
    {
        // IMPORTANT: pathfinding often queries neighbors out of bounds; treat as not passable.
        if (x < 0 || y < 0 || x >= w || y >= h)
            return false;

        return pass[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] != 0;
    }

    void SetPassable(int32_t x, int32_t y, bool isPassable)
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;

        pass[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = isPassable ? 1 : 0;
    }
};

// Draw a simple axis-aligned/diagonal "line" and mark cells as blocked.
// (Matches your original usage where you only used straight horizontal lines.)
static void BlockLine(NavTestGrid& g, int x0, int y0, int x1, int y1)
{
    const int dx = (x1 > x0) - (x1 < x0);
    const int dy = (y1 > y0) - (y1 < y0);

    int x = x0, y = y0;
    for (;;)
    {
        g.SetPassable(static_cast<int32_t>(x), static_cast<int32_t>(y), false);
        if (x == x1 && y == y1) break;
        x += dx;
        y += dy;
    }
}

} // namespace colony_smoke_nav_test

TEST_CASE("Navigator: simple open grid path")
{
    colony_smoke_nav_test::NavTestGrid g(32, 32);

    // FIX: Navigator has no default ctor and (per your compile errors) expects (IGridMap&, Options).
    colony::nav::Navigator nav(static_cast<const colony::nav::IGridMap&>(g), colony::nav::Navigator::Options{});

    auto p = nav.FindPath({0, 0}, {31, 31});

    REQUIRE(p.has_value());
    CHECK_FALSE(p->points.empty());
    CHECK(p->points.front().x == 0);
    CHECK(p->points.front().y == 0);
    CHECK(p->points.back().x == 31);
    CHECK(p->points.back().y == 31);
}

TEST_CASE("Navigator: wall with gap (no corner cutting)")
{
    colony_smoke_nav_test::NavTestGrid g(32, 32);

    // horizontal wall at y=15, open gap at x=16
    colony_smoke_nav_test::BlockLine(g, 0, 15, 30, 15);
    g.SetPassable(16, 15, true); // open the gap

    colony::nav::Navigator nav(static_cast<const colony::nav::IGridMap&>(g), colony::nav::Navigator::Options{});
    auto p = nav.FindPath({4, 10}, {28, 20});

    REQUIRE(p.has_value());

    bool crossedGap = false;
    for (const auto& c : p->points)
    {
        if (c.y == 15 && c.x == 16)
        {
            crossedGap = true;
            break;
        }
    }
    CHECK(crossedGap);
}

TEST_CASE("Navigator: multi-cluster path between clusters")
{
    colony_smoke_nav_test::NavTestGrid g(96, 96);

    // Build a guaranteed-connected corridor network spanning multiple clusters:
    // - vertical corridor at x=48
    // - horizontal corridor from start (4,4) to x=48 at y=4
    // - horizontal corridor from x=48 to end (90,90) at y=90
    for (int y = 0; y < 96; ++y)
    {
        for (int x = 0; x < 96; ++x)
            g.SetPassable(x, y, false);
    }

    for (int y = 0; y < 96; ++y)
        g.SetPassable(48, y, true);

    for (int x = 4; x <= 48; ++x)
        g.SetPassable(x, 4, true);

    for (int x = 48; x <= 90; ++x)
        g.SetPassable(x, 90, true);

    colony::nav::Navigator::Options opt;
    opt.cluster.clusterW     = 32;
    opt.cluster.clusterH     = 32;
    opt.cluster.portalStride = 8;

    colony::nav::Navigator nav(static_cast<const colony::nav::IGridMap&>(g), opt);
    auto p = nav.FindPath({4, 4}, {90, 90});

    CHECK(p.has_value());
}

TEST_CASE("ClusterGrid: repeated queries don't bias the abstract graph with stale temp edges")
{
    using namespace colony_smoke_nav_test;

    // Map layout (64x32) split into 2 clusters horizontally (32x32):
    //  - Left cluster: row y=16 is a wall except a single gap at x=2.
    //  - Right cluster: row y=16 is a solid wall (no gap).
    // This creates a situation where:
    //  - goal at (60,8) is reachable via the "upper" border portals.
    //  - goal at (60,24) is reachable via "lower" border portals.
    //  - BUT the goal at (60,24) is NOT reachable from an upper portal *within the right cluster*.
    //
    // The old implementation permanently added portal->temp edges and reused temp IDs between
    // queries, leaving stale edge weights in the portal graph. That could bias the A* over the
    // abstract graph into choosing an upper portal for the second query, yielding a huge detour.

    NavTestGrid g(64, 32);

    // Carve the horizontal walls.
    for (int x = 0; x < 64; ++x)
    {
        if (x >= 32)
        {
            // Right cluster: solid wall.
            g.SetPassable(x, 16, false);
        }
        else
        {
            // Left cluster: wall with a single gap.
            g.SetPassable(x, 16, x == 2);
        }
    }

    colony::nav::ClusterGridSettings s;
    s.clusterW = 32;
    s.clusterH = 32;
    s.portalStride = 4;
    s.diagonals = colony::nav::DiagonalPolicy::AllowedIfNoCut;

    colony::nav::ClusterGrid cluster(static_cast<const colony::nav::IGridMap&>(g), s);

    const colony::nav::Coord start{2, 2};
    const colony::nav::Coord goalTop{60, 8};
    const colony::nav::Coord goalBottom{60, 24};

    // 1) Warm-up query (build portals + cache intra-cluster edges).
    auto p1 = cluster.FindPath(start, goalTop);
    REQUIRE(p1.has_value());

    // 2) Second query: should not be biased by stale portal->temp edges.
    auto p2 = cluster.FindPath(start, goalBottom);
    REQUIRE(p2.has_value());

    // Sanity.
    CHECK(p2->points.front() == start);
    CHECK(p2->points.back() == goalBottom);

    // With correct per-query temp edges, the path is ~80 steps (down through the gap, then right).
    // If stale temp edges bias the abstract plan, it can detour into the upper portal first,
    // exploding the path length well past 120.
    CHECK(p2->points.size() < 120);
}
