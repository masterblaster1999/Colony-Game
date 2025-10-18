// src/worldgen/stages/Scatter.cpp

#include "worldgen/WorldGen.hpp"      // ScatterStage declaration lives here
#include "worldgen/StageContext.hpp"  // StageContext (ctx)
#include "worldgen/Random.hpp"        // project RNG + sub_rng helpers

#include <random>
#include <cstdint>   // std::uint64_t
#include <algorithm> // (optional expansions later)

namespace colony::worldgen {

void ScatterStage::generate(StageContext& ctx)
{
    // Derive a local RNG for this stage (deterministic, isolated).
    // Keeps stages reproducible and avoids cross-stage RNG coupling.
    [[maybe_unused]] auto rng = ctx.sub_rng(StageId::Scatter, "Scatter");

    // ---------------------------------------------------------------------
    // Minimal skeleton: at this point you have a deterministic RNG "rng".
    // Expand below with your real scatter logic (trees, rocks, berries, etc.).
    // ---------------------------------------------------------------------

    /*
    // Example (commented): if you expose chunk dimensions & placement helpers
    const int w = ctx.chunkWidth();     // <- or ctx.map().width()
    const int h = ctx.chunkHeight();    // <- or ctx.map().height()

    // If your RNG wrapper provides uniform distributions, prefer those.
    std::uniform_int_distribution<int> X(0, w - 1), Y(0, h - 1);

    // Derive a tiny density from settings; tweak as needed.
    const int tries = std::max(1, (w * h) / 64);
    for (int i = 0; i < tries; ++i) {
        const int x = X(rng);
        const int y = Y(rng);
        if (ctx.canPlaceScatter(x, y)) {
            ctx.placeScatter(x, y, ScatterType::Rock); // or Tree/Bush/etc.
        }
    }
    */

    // Keep function side-effect free until you add real placement code.
}

} // namespace colony::worldgen
