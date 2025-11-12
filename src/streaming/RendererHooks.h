#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <string>

// Forward-declare a DDS blob container to keep headers light.
namespace DirectX { class ScratchImage; }

struct HeightTileCPU {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> r16; // little-endian heights
};

struct TextureTileCPU {
    // DDS already decoded into ScratchImage on CPU; renderer will create GPU texture & SRV.
    const DirectX::ScratchImage* scratch = nullptr; // Not owning; see TileIO lifetime notes.
};

struct ITerrainRenderer {
    virtual ~ITerrainRenderer() = default;

    // Called on the main thread to finalize GPU creation/copy.
    virtual void UploadHeightTile(const TileCoord& key, const HeightTileCPU& ht) = 0;

    // Create/update SRV for albedo/normal; return an engine handle if you like (not required here).
    virtual void UploadTextureTile(const TileCoord& key, TileKind kind, const TextureTileCPU& img) = 0;

    // Evict resources (GPU) for tiles that are getting dropped.
    virtual void EvictTile(const TileCoord& key, TileKind kind) = 0;
};
