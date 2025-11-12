#include "TileIO.h"
#include "RendererHooks.h"
#include <fstream>
#include <DirectXTex.h> // DirectXTex; DDS I/O + ScratchImage. :contentReference[oaicite:5]{index=5}

struct HeightTileCPUImpl : public HeightTileCPU { };

std::unique_ptr<HeightTileCPU> LoadHeightTileR16(const std::filesystem::path& file)
{
    auto ht = std::make_unique<HeightTileCPUImpl>();
    ht->width  = kTileSamples;
    ht->height = kTileSamples;
    const size_t bytes = size_t(ht->width) * size_t(ht->height) * sizeof(uint16_t);
    ht->r16.resize(bytes / sizeof(uint16_t));

    std::ifstream f(file, std::ios::binary);
    if (!f) return nullptr;
    f.read(reinterpret_cast<char*>(ht->r16.data()), std::streamsize(bytes));
    if (!f) return nullptr;
    return ht;
}

// DDS loader keeps an owned ScratchImage for the renderer to consume.
// You can also use DDSTextureLoader if you prefer creating GPU resources directly. :contentReference[oaicite:6]{index=6}
std::unique_ptr<DdsOwned> LoadDdsOwned(const std::filesystem::path& file)
{
    auto out = std::make_unique<DdsOwned>();
    auto owned = std::make_unique<DirectX::ScratchImage>();
    DirectX::TexMetadata md{};
    const auto hr = DirectX::LoadFromDDSFile(file.c_str(), DirectX::DDS_FLAGS_NONE, &md, *owned);
    if (FAILED(hr)) return nullptr;
    out->img = std::move(owned);
    return out;
}
