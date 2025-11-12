#pragma once
#include "TileTypes.h"
#include "InstallPaths.h"
#include <filesystem>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declare DirectXTex types; we include the header in the .cpp.
namespace DirectX { class ScratchImage; }

struct HeightTileCPU;
struct TextureTileCPU;

// Loaders return nullptr on failure and fill out-of-band error strings if you want (omitted here).
std::unique_ptr<HeightTileCPU> LoadHeightTileR16(const std::filesystem::path& file);

struct DdsOwned {
    std::unique_ptr<DirectX::ScratchImage> img; // owns the ScratchImage
};

std::unique_ptr<DdsOwned> LoadDdsOwned(const std::filesystem::path& file);
