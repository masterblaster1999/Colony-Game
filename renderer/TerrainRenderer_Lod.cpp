// renderer/TerrainRenderer_Lod.cpp
#include "TerrainRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

using namespace DirectX;

namespace colony::render
{

TerrainLod::TerrainLod(Settings settings)
    : m_settings(settings)
{
}

void TerrainLod::rebuildPatches(const TerrainGeometry& geometry)
{
    m_patches.clear();
    m_visible.clear();

    const uint32_t width  = geometry.width();
    const uint32_t height = geometry.height();

    if (width < 2 || height < 2)
        return;

    const float cellSize = geometry.cellSize();
    if (cellSize <= 0.0f)
        return;

    const auto& verts = geometry.vertices();
    assert(verts.size() == static_cast<std::size_t>(width) * height);

    // Number of cells (quads) along each axis.
    const uint32_t cellsX = width  - 1;
    const uint32_t cellsZ = height - 1;

    // How many cells per patch (approx) along one edge.
    uint32_t cellsPerPatch = static_cast<uint32_t>(
        std::max(1.0f, std::round(m_settings.patchWorldSize / cellSize))
    );
    if (cellsPerPatch == 0)
        cellsPerPatch = 1;

    const uint32_t patchCountX = (cellsX + cellsPerPatch - 1) / cellsPerPatch;
    const uint32_t patchCountZ = (cellsZ + cellsPerPatch - 1) / cellsPerPatch;

    m_patches.reserve(static_cast<std::size_t>(patchCountX) * patchCountZ);

    for (uint32_t pz = 0; pz < patchCountZ; ++pz)
    {
        const uint32_t cellZStart = pz * cellsPerPatch;
        const uint32_t cellZEnd   = std::min(cellZStart + cellsPerPatch, cellsZ);

        for (uint32_t px = 0; px < patchCountX; ++px)
        {
            const uint32_t cellXStart = px * cellsPerPatch;
            const uint32_t cellXEnd   = std::min(cellXStart + cellsPerPatch, cellsX);

            // Scan the vertices covered by this patch to compute min/max Y.
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();

            for (uint32_t z = cellZStart; z <= cellZEnd; ++z)
            {
                for (uint32_t x = cellXStart; x <= cellXEnd; ++x)
                {
                    const std::size_t idx = static_cast<std::size_t>(z) * width + x;
                    assert(idx < verts.size());
                    minY = std::min(minY, verts[idx].position.y);
                    maxY = std::max(maxY, verts[idx].position.y);
                }
            }

            // World-space bounds for this patch.
            const float minX = static_cast<float>(cellXStart) * cellSize;
            const float maxX = static_cast<float>(cellXEnd + 1) * cellSize;
            const float minZ = static_cast<float>(cellZStart) * cellSize;
            const float maxZ = static_cast<float>(cellZEnd + 1) * cellSize;

            TerrainPatch patch{};
            patch.boundsMin = XMFLOAT3(minX, minY, minZ);
            patch.boundsMax = XMFLOAT3(maxX, maxY, maxZ);
            patch.lodLevel  = 0;

            // We don't know your index-buffer packing scheme here, so we leave
            // startIndex/indexCount at 0. You can fill them in from your renderer
            // if you use per-patch index ranges.
            patch.startIndex = 0;
            patch.indexCount = 0;

            m_patches.push_back(patch);
        }
    }
}

void TerrainLod::updateVisiblePatches(const XMFLOAT3& cameraPosition)
{
    m_visible.clear();
    if (m_patches.empty())
        return;

    const float lod0Dist = m_settings.lod0Distance;
    const float lod1Dist = m_settings.lod1Distance;
    const float lod2Dist = m_settings.lod2Distance;
    const float lod3Dist = m_settings.lod3Distance;

    for (auto& patch : m_patches)
    {
        // Center of patch in world space.
        const XMFLOAT3 min = patch.boundsMin;
        const XMFLOAT3 max = patch.boundsMax;

        const float centerX = 0.5f * (min.x + max.x);
        const float centerZ = 0.5f * (min.z + max.z);

        const float dx = cameraPosition.x - centerX;
        const float dz = cameraPosition.z - centerZ;
        const float distXZ = std::sqrt(dx * dx + dz * dz);

        uint32_t lod = 3;
        if (distXZ < lod0Dist)      lod = 0;
        else if (distXZ < lod1Dist) lod = 1;
        else if (distXZ < lod2Dist) lod = 2;
        else                        lod = 3;

        patch.lodLevel = lod;

        // Simple distance culling: drop patches beyond the furthest LOD distance.
        if (distXZ <= lod3Dist)
        {
            m_visible.push_back(&patch);
        }
    }
}

} // namespace colony::render
