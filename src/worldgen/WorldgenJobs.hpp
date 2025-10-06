#pragma once
#include <future>
#include <cstdint>
#include <vector>
#include <utility>
#include "../common/ThreadPool.hpp"

namespace colony::worldgen {

// Example: generate a heightmap tile (chunk) given coordinates + seed.
struct ChunkCoords { int cx{}, cy{}; };
struct Heightmap {
    int w{}, h{};
    std::vector<float> samples; // row-major
};

struct ChunkRequest {
    ChunkCoords coords;
    std::uint64_t worldSeed = 0;
    int size = 64; // side length
};

using GenerateChunkFn = Heightmap(*)(const ChunkRequest&);

inline std::future<Heightmap>
SubmitChunkJob(colony::ThreadPool& pool, ChunkRequest req, GenerateChunkFn gen) {
    return pool.submit([req, gen]() {
        return gen(req);
    });
}

} // namespace colony::worldgen
