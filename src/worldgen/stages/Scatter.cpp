// src/worldgen/stages/Scatter.cpp
#include "worldgen/WorldGen.hpp"

namespace colony::worldgen {

// NOTE: The current WorldChunk in your repo exposes height/temperature/moisture/flow/biome.
// If/when you add a decorations/props buffer, wire it here. For now this is a no-op
// placeholder stage so enabling/disabling Scatter in settings remains harmless.
void ScatterStage::generate(StageContext& ctx)
{
    (void)ctx;
    // Intentionally empty.
    // Example for the future:
    //  - pick a per-biome scatter density
    //  - sample a few candidates with ctx.rng.next_float01()
    //  - place decorations into ctx.out.props (or similar)
}

} // namespace colony::worldgen
