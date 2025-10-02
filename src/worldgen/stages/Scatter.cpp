#include "ScatterStage.hpp"   // your per-stage header from earlier
// "ScatterStage.hpp" already includes "StagesTypes.hpp" and "StageContext.hpp".

// Prefer your project's RNG if available; otherwise use the C++ standard engine.
#if __has_include("Random.hpp")
  #include "Random.hpp"
  #define COLONY_HAVE_CUSTOM_RANDOM 1
#else
  #include <random>
#endif

#include <cstdint>   // std::uint64_t
#include <algorithm> // (optional expansions later)

namespace colony::worldgen {

void ScatterStage::generate(StageContext& ctx)
{
    // --- 1) Pull the per-stage seed from the context.
    // Assumes WorldGenerator::run_ called ctx.reseed(StageSeed(...)) beforehand.
    // If your StageContext named it differently, change "seed()" accordingly.
    const std::uint64_t seed = ctx.seed();

    // --- 2) Spin up a *local* RNG for this stage invocation.
    //       This keeps stages deterministic and isolated from each other.
    #if COLONY_HAVE_CUSTOM_RANDOM
        [[maybe_unused]] Random rng{ seed };
    #else
        [[maybe_unused]] std::mt19937_64 rng{ seed };
    #endif

    // ---------------------------------------------------------------------
    // Minimal skeleton: at this point you have a deterministic RNG "rng".
    // Expand below with your real scatter logic (trees, rocks, berries, etc.).
    // ---------------------------------------------------------------------

    /*
    // Example (commented): if you expose chunk dimensions & placement helpers
    const int w = ctx.chunkWidth();     // <- or ctx.map().width()
    const int h = ctx.chunkHeight();    // <- or ctx.map().height()

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
