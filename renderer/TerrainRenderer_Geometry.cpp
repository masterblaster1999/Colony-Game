// renderer/TerrainRenderer_Geometry.cpp
#include "TerrainRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

using namespace DirectX;

namespace colony::render
{

void TerrainGeometry::buildRegularGrid(
    uint32_t width,
    uint32_t height,
    float    cellSize,
    std::span<const float> heightSamples
)
{
    if (width < 2 || height < 2)
    {
        throw std::invalid_argument("TerrainGeometry::buildRegularGrid: width and height must be >= 2");
    }

    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (heightSamples.size() != expected)
    {
        throw std::invalid_argument("TerrainGeometry::buildRegularGrid: heightSamples.size() != width * height");
    }

    m_width    = width;
    m_height   = height;
    m_cellSize = cellSize;

    m_vertices.clear();
    m_vertices.resize(expected);

    // Fill vertex positions and initial normals/UVs.
    for (uint32_t z = 0; z < height; ++z)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const std::size_t idx = static_cast<std::size_t>(z) * width + x;

            const float worldX = static_cast<float>(x) * cellSize;
            const float worldZ = static_cast<float>(z) * cellSize;
            const float worldY = heightSamples[idx];

            const float u = (width  > 1) ? static_cast<float>(x) / static_cast<float>(width  - 1) : 0.0f;
            const float v = (height > 1) ? static_cast<float>(z) / static_cast<float>(height - 1) : 0.0f;

            TerrainVertex vert{};
            vert.position = XMFLOAT3(worldX, worldY, worldZ);
            vert.normal   = XMFLOAT3(0.0f, 1.0f, 0.0f); // temporary; will be recomputed
            vert.uv       = XMFLOAT2(u, v);

            m_vertices[idx] = vert;
        }
    }

    // Build triangle indices (two triangles per quad, row-major).
    const uint32_t quadsX = width  - 1;
    const uint32_t quadsZ = height - 1;

    m_indices.clear();
    m_indices.reserve(static_cast<std::size_t>(quadsX) * quadsZ * 6u);

    for (uint32_t z = 0; z < quadsZ; ++z)
    {
        for (uint32_t x = 0; x < quadsX; ++x)
        {
            const uint32_t i0 = z * width + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + width;
            const uint32_t i3 = i2 + 1;

            // Triangle 1 (i0, i2, i1)
            m_indices.push_back(i0);
            m_indices.push_back(i2);
            m_indices.push_back(i1);

            // Triangle 2 (i1, i2, i3)
            m_indices.push_back(i1);
            m_indices.push_back(i2);
            m_indices.push_back(i3);
        }
    }

    // Finally, compute normals from triangles.
    recalculateNormals();
}

void TerrainGeometry::recalculateNormals()
{
    if (m_vertices.empty() || m_indices.empty())
        return;

    // Reset normals to zero.
    for (auto& v : m_vertices)
    {
        v.normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
    }

    const std::size_t triCount = m_indices.size() / 3;
    for (std::size_t tri = 0; tri < triCount; ++tri)
    {
        const uint32_t i0 = m_indices[tri * 3 + 0];
        const uint32_t i1 = m_indices[tri * 3 + 1];
        const uint32_t i2 = m_indices[tri * 3 + 2];

        assert(i0 < m_vertices.size());
        assert(i1 < m_vertices.size());
        assert(i2 < m_vertices.size());

        const XMVECTOR p0 = XMLoadFloat3(&m_vertices[i0].position);
        const XMVECTOR p1 = XMLoadFloat3(&m_vertices[i1].position);
        const XMVECTOR p2 = XMLoadFloat3(&m_vertices[i2].position);

        const XMVECTOR e1 = XMVectorSubtract(p1, p0);
        const XMVECTOR e2 = XMVectorSubtract(p2, p0);

        XMVECTOR n = XMVector3Cross(e1, e2);
        // We don't normalize per triangle; accumulate then normalize per vertex.
        for (uint32_t idx : {i0, i1, i2})
        {
            XMVECTOR vN = XMLoadFloat3(&m_vertices[idx].normal);
            vN = XMVectorAdd(vN, n);
            XMStoreFloat3(&m_vertices[idx].normal, vN);
        }
    }

    // Normalize accumulated normals.
    for (auto& v : m_vertices)
    {
        XMVECTOR n = XMLoadFloat3(&v.normal);
        n = XMVector3Normalize(n);
        XMStoreFloat3(&v.normal, n);
    }
}

} // namespace colony::render
