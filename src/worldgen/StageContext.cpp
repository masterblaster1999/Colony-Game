#include "worldgen/StageContext.hpp"

// Pull in full type definitions here only.
#include "worldgen/WorldGen.hpp"   // for WorldChunk, ChunkCoord (adjust if your types live elsewhere)

namespace colony::worldgen {

StageContext::StageContext(const GeneratorSettings& s,
                           const ChunkCoord&       coord,
                           Pcg32                   r,
                           WorldChunk&             o) noexcept
: settings(s)
, chunkX(coord.x)   // adjust to coord.{x,y} or coord.{cx,cy} as defined in your type
, chunkY(coord.y)
, rng(r)
, out(o)
, chunk(o)
{
    // Read dimensions from the chunk's height grid.
    // Adjust if your grid accessors use different names.
    width  = out.height.width();
    height = out.height.height();
}

} // namespace colony::worldgen
