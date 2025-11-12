#pragma once
#include <cstdint>
#include <functional>

struct TileCoord {
    int32_t x = 0;
    int32_t y = 0;
    int32_t lod = 0; // keep for future LOD levels
    bool operator==(const TileCoord& o) const noexcept { return x==o.x && y==o.y && lod==o.lod; }
};

struct TileCoordHasher {
    size_t operator()(const TileCoord& k) const noexcept {
        const uint64_t a = (uint32_t)k.x;
        const uint64_t b = (uint32_t)k.y;
        const uint64_t c = (uint32_t)k.lod;
        // simple mix:
        return static_cast<size_t>((a * 11400714819323198485ull) ^ (b << 1) ^ (c << 9));
    }
};

enum class TileKind : uint8_t { Height, Albedo, Normal };

static constexpr int kTileSamples = 256; // samples or pixels per tile
