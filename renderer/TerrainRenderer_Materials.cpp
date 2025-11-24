// renderer/TerrainRenderer_Materials.cpp
#include "TerrainRenderer.h"

#include <stdexcept>

namespace colony::render
{

void TerrainMaterials::setLayer(size_t idx, const TerrainMaterialLayer& layer)
{
    if (idx >= MaxLayers)
    {
        throw std::out_of_range("TerrainMaterials::setLayer: idx >= MaxLayers");
    }
    m_layers[idx] = layer;
}

const TerrainMaterialLayer& TerrainMaterials::layer(size_t idx) const
{
    if (idx >= MaxLayers)
    {
        throw std::out_of_range("TerrainMaterials::layer: idx >= MaxLayers");
    }
    return m_layers[idx];
}

void TerrainMaterials::setSplatMap(
    uint32_t width,
    uint32_t height,
    std::span<const std::array<std::uint8_t, 4>> weights
)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("TerrainMaterials::setSplatMap: width and height must be > 0");
    }

    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height);
    if (weights.size() != expected)
    {
        throw std::invalid_argument("TerrainMaterials::setSplatMap: weights.size() != width * height");
    }

    m_splatWidth  = width;
    m_splatHeight = height;
    m_splat.assign(weights.begin(), weights.end());
}

} // namespace colony::render
