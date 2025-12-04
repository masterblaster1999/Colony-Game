// tests/worldgen/test_grid_index.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "worldgen/detail/GridIndex.hpp"

TEST_CASE("inb bounds") {
    using worldgen::detail::inb;
    CHECK(inb(0, 10));
    CHECK(inb(9, 10));
    CHECK_FALSE(inb(10, 10));
    CHECK(inb(2, 3, 10, 20));
    CHECK_FALSE(inb(10, 3, 10, 20));
}

TEST_CASE("index3 layout") {
    using worldgen::detail::index3;
    const std::size_t sx = 4, sy = 3, sz = 2;
    // (x=1,y=2,z=0) => (0*3+2)*4+1 = 9
    CHECK(index3(1,2,0, sx,sy,sz) == 9);
    // (x=0,y=0,z=1) => (1*3+0)*4+0 = 12
    CHECK(index3(0,0,1, sx,sy,sz) == 12);
}
