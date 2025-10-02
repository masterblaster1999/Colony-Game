// tests/test_worldgen_ctx.cpp
#include "worldgen/WorldGen.hpp"
#include "worldgen/StageContext.hpp"
#include <cassert>

using namespace colony::worldgen;

int main() {
  GeneratorSettings s{};
  s.worldSeed = 42;
  WorldGenerator gen(s);
  auto a = gen.generate({0,0});
  auto b = gen.generate({0,0});
  assert(a.height.width() == b.height.width());
  assert(a.height.height() == b.height.height());
  // …consider hashing a few fields to assert equality if operator== isn't defined
  return 0;
}
