// tests/test_worldgen_ctx.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/WorldChunk.hpp"
#include <cassert>
using namespace colony::worldgen;

int main() {
    GeneratorSettings s{};
    s.worldSeed = 42;
    WorldGenerator gen(s);
    auto a = gen.generate(ChunkCoord{0, 0});
    auto b = gen.generate(ChunkCoord{0, 0});
    assert(a.height.width() == b.height.width());
    assert(a.height.height() == b.height.height());
    return 0;
}
