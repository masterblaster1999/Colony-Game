#pragma once
#include <cstdint>
#include <vector>

namespace colony::worldgen {

// What the generator needs at runtime.
struct GeneratorSettings {
    // grid / chunk layout
    int         cellsPerChunk   { 64 };        // default: 64x64
    // toggles
    bool        enableHydrology { true };
    bool        enableScatter   { true };
    // seeds
    std::uint64_t worldSeed     { 0 };
};

// Absolute chunk coordinate in the world grid.
struct ChunkCoord {
    int x{0};
    int y{0};
};

// Minimal 2D array the stages can write into.  Keep this header-light.
template <class T>
struct Grid2D {
    int w{0}, h{0};
    std::vector<T> data;
    void resize(int W, int H, const T& v = T{}) {
        w = W; h = H; data.assign(std::size_t(W) * std::size_t(H), v);
    }
    [[nodiscard]] int width()  const noexcept { return w; }
    [[nodiscard]] int height() const noexcept { return h; }
    [[nodiscard]] T&       at(int X, int Y)       { return data[std::size_t(Y)*w + X]; }
    [[nodiscard]] const T& at(int X, int Y) const { return data[std::size_t(Y)*w + X]; }
};

// Payload for a generated chunk.  Matches fields used by stages/renderer.
struct WorldChunk {
    ChunkCoord          coord{};
    Grid2D<float>       height;
    Grid2D<float>       temperature;
    Grid2D<float>       moisture;
    Grid2D<float>       flow;
    Grid2D<std::uint8_t> biome;
};

} // namespace colony::worldgen
