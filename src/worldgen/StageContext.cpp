#include "worldgen/StageContext.hpp"
#include "worldgen/WorldChunk.hpp"

namespace colony::worldgen {

StageContext::StageContext(const GeneratorSettings& s,
                           const ChunkCoord& coord,
                           Pcg32 r,
                           WorldChunk& o) noexcept
    : settings(s), chunkX(coord.x), chunkY(coord.y), rng(r), out(o), chunk(o)
{
    width  = out.height.width();
    height = out.height.height();
}

} // namespace colony::worldgen
