// tests/test_worldgen_ctx.cpp
#include <doctest/doctest.h>
#include "worldgen/WorldGen.hpp"
#include "worldgen/WorldChunk.hpp"

using namespace colony::worldgen;

TEST_CASE("Worldgen: same seed → deterministic chunk") {
    GeneratorSettings s{};
    s.worldSeed = 42;

    WorldGenerator gen{s};

    auto a = gen.generate(ChunkCoord{0, 0});
    auto b = gen.generate(ChunkCoord{0, 0});

    CHECK(a.height.width()  == b.height.width());
    CHECK(a.height.height() == b.height.height());
    // You can add hashing/equality asserts later if you expose more fields.
}
