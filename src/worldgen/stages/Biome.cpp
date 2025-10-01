// src/worldgen/stages/Biome.cpp
#include "worldgen/WorldGen.hpp"
#include <cstdint>

namespace colony::worldgen {

void BiomeStage::generate(StageContext& ctx)
{
    const int N = ctx.out.height.width();
    auto& T = ctx.out.temperature;  // degrees C (float)
    auto& M = ctx.out.moisture;     // [0..1]
    auto& B = ctx.out.biome;        // uint8 grid

    auto classify = [](float tempC, float moist) -> std::uint8_t {
        // Simple Whittaker-like partition; tune later.
        if (moist < 0.20f) return (tempC > 20.f) ? 1 /*Desert*/        : 2 /*Cold Steppe*/;
        if (moist < 0.45f) return (tempC > 15.f) ? 3 /*Savanna*/       : 4 /*Shrubland*/;
        if (moist < 0.70f) return (tempC >  5.f) ? 5 /*TemperateFor.*/ : 6 /*Boreal*/;
        return                 (tempC >  0.f) ? 7 /*Rainforest*/      : 8 /*Tundra*/;
    };

    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            B.at(x, y) = classify(T.at(x, y), M.at(x, y));
}

} // namespace colony::worldgen
