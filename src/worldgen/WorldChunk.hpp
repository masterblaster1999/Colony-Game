#pragma once
#include <cstdint>
#include "worldgen/Grid2D.hpp"

namespace colony::worldgen {

// Minimal integer 2D coordinate for chunk indexing.
struct ChunkCoord {
    int x = 0;
    int y = 0;
};

// The concrete, complete type used by stages.
// Climate/Hydrology/Biome/Scatter read/write these fields directly.
struct WorldChunk {
    Grid2D<float> height;       // elevation/heightfield [0..1] or meters
    Grid2D<float> temperature;  // degrees C (or normalized)
    Grid2D<float> moisture;     // [0..1]
    ChunkCoord    coord;        // chunk coordinate in world space

    WorldChunk() = default;

    explicit WorldChunk(int n, ChunkCoord c = {})
        : height(n, n, 0.0f)
        , temperature(n, n, 0.0f)
        , moisture(n, n, 0.0f)
        , coord(c) {}

    [[nodiscard]] int size() const noexcept { return height.width(); }
};

} // namespace colony::worldgen
