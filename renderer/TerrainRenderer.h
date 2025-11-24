// renderer/TerrainRenderer.h
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <DirectXMath.h>

namespace colony::render
{

// Simple terrain vertex layout; adapt to your HLSL input layout.
struct TerrainVertex
{
    DirectX::XMFLOAT3 position; // (x, y, z) in world space
    DirectX::XMFLOAT3 normal;   // normalized
    DirectX::XMFLOAT2 uv;       // (0..1, 0..1) across full heightmap
};

// Metadata for a single terrain patch, used by the LOD system.
struct TerrainPatch
{
    uint32_t startIndex   = 0;  // optional: index buffer range for this patch
    uint32_t indexCount   = 0;  // optional: index buffer range for this patch
    uint32_t lodLevel     = 0;  // 0 = highest detail

    DirectX::XMFLOAT3 boundsMin{}; // axis-aligned bounding box (world space)
    DirectX::XMFLOAT3 boundsMax{};
};

// -----------------------------------------------------------------------------
// Geometry: builds a regular grid mesh from a heightmap.
// -----------------------------------------------------------------------------
class TerrainGeometry
{
public:
    TerrainGeometry() = default;

    // Build a regular grid (width x height vertices) from a heightmap.
    // `heightSamples` must be width * height elements in row-major order.
    void buildRegularGrid(
        uint32_t width,
        uint32_t height,
        float    cellSize,
        std::span<const float> heightSamples
    );

    // Recompute vertex normals from the index buffer (must be built already).
    void recalculateNormals();

    [[nodiscard]] const std::vector<TerrainVertex>& vertices() const noexcept
    {
        return m_vertices;
    }

    [[nodiscard]] const std::vector<uint32_t>& indices() const noexcept
    {
        return m_indices;
    }

    [[nodiscard]] uint32_t width() const noexcept  { return m_width;  }
    [[nodiscard]] uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] float    cellSize() const noexcept { return m_cellSize; }

private:
    uint32_t m_width     = 0;   // vertex grid width
    uint32_t m_height    = 0;   // vertex grid height
    float    m_cellSize  = 1.0f;

    std::vector<TerrainVertex> m_vertices;
    std::vector<uint32_t>      m_indices;
};

// -----------------------------------------------------------------------------
// Materials: high-level description + CPU-side splat map storage.
// -----------------------------------------------------------------------------
struct TerrainMaterialLayer
{
    // These are IDs/indices into your engine's texture system (not raw D3D12 resources).
    uint32_t albedoTextureId    = 0;
    uint32_t normalTextureId    = 0;
    uint32_t roughnessTextureId = 0;

    float    uvScale            = 1.0f; // tiling factor
};

class TerrainMaterials
{
public:
    static constexpr size_t MaxLayers = 4;

    void setLayer(size_t idx, const TerrainMaterialLayer& layer);
    [[nodiscard]] const TerrainMaterialLayer& layer(size_t idx) const;

    // Splat map: 4-channel weights per texel (R,G,B,A) -> up to 4 material layers.
    // weights.size() must equal width * height.
    void setSplatMap(
        uint32_t width,
        uint32_t height,
        std::span<const std::array<std::uint8_t, 4>> weights
    );

    [[nodiscard]] uint32_t splatWidth()  const noexcept { return m_splatWidth;  }
    [[nodiscard]] uint32_t splatHeight() const noexcept { return m_splatHeight; }
    [[nodiscard]] const std::vector<std::array<std::uint8_t, 4>>&
        splatData() const noexcept { return m_splat; }

private:
    std::array<TerrainMaterialLayer, MaxLayers> m_layers{};

    uint32_t m_splatWidth  = 0;
    uint32_t m_splatHeight = 0;
    std::vector<std::array<std::uint8_t, 4>> m_splat;
};

// -----------------------------------------------------------------------------
// LOD / patch selection: chunked regular-grid patches + distance-based LOD.
// -----------------------------------------------------------------------------
class TerrainLod
{
public:
    struct Settings
    {
        // Approximate world-space size of one patch edge (meters/tiles).
        float patchWorldSize = 32.0f;

        // View-distance thresholds for different LODs (XZ plane distance).
        float lod0Distance   = 50.0f;   // full detail
        float lod1Distance   = 100.0f;
        float lod2Distance   = 200.0f;
        float lod3Distance   = 400.0f;  // beyond this, everything is lowest LOD
    };

    explicit TerrainLod(Settings settings = {});

    // Rebuild the patch grid from the terrain geometry.
    // Uses geometry.width()/height()/cellSize() and vertex positions to build AABBs.
    void rebuildPatches(const TerrainGeometry& geometry);

    // Update which patches are visible and what their LOD level should be.
    // Only uses camera position (XZ distance); you can add frustum culling later.
    void updateVisiblePatches(const DirectX::XMFLOAT3& cameraPosition);

    [[nodiscard]] const std::vector<TerrainPatch>& patches() const noexcept
    {
        return m_patches;
    }

    [[nodiscard]] const std::vector<const TerrainPatch*>& visiblePatches() const noexcept
    {
        return m_visible;
    }

    [[nodiscard]] const Settings& settings() const noexcept { return m_settings; }
    void setSettings(const Settings& s) noexcept { m_settings = s; }

private:
    Settings m_settings;

    std::vector<TerrainPatch>         m_patches;
    std::vector<const TerrainPatch*>  m_visible;
};

} // namespace colony::render
