#include "TerrainMesh.hpp"
#include <cmath>
#include <algorithm>
#include <cassert>

namespace colony::worldgen {

// Pack RGBA8 as 0xRRGGBBAA for easy debugging; change if your pipeline expects a different byte order.
static inline std::uint32_t packRGBA8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a){
    return (static_cast<std::uint32_t>(r) << 24) |
           (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) <<  8) |
           (static_cast<std::uint32_t>(a));
}

// A compact biome → color palette (inspired by common Whittaker diagram colorings).
// Feel free to tweak to match your art direction.
static inline std::uint32_t biomeColor(std::uint8_t biomeId){
    switch (biomeId) {
        case 1: /* Desert            */ return packRGBA8(0xE9,0xD8,0xA6,0xFF);
        case 2: /* Cold Steppe       */ return packRGBA8(0xB7,0xA6,0x9F,0xFF);
        case 3: /* Savanna           */ return packRGBA8(0xC8,0xDC,0x6C,0xFF);
        case 4: /* Shrubland         */ return packRGBA8(0xA3,0xB1,0x8A,0xFF);
        case 5: /* Temperate Forest  */ return packRGBA8(0x66,0xA1,0x82,0xFF);
        case 6: /* Boreal            */ return packRGBA8(0x55,0x6B,0x2F,0xFF);
        case 7: /* Rainforest        */ return packRGBA8(0x00,0x6D,0x2C,0xFF);
        case 8: /* Tundra            */ return packRGBA8(0xB8,0xDE,0xE6,0xFF);
        default:                      return packRGBA8(0x88,0x88,0x88,0xFF); // unknown/sea/etc.
    }
}

MeshData BuildTerrainMesh(const WorldChunk& chunk,
                          const TerrainMeshParams& params,
                          HeightSampleFn neighbor)
{
    MeshData mesh;

    const int N = chunk.height.width();
    assert(N == chunk.height.height() && "Height grid must be square");

    const float cs     = params.cellSizeMeters;
    const float hScale = params.heightScale;

    // Reserve to avoid reallocations:
    mesh.vertices.reserve(static_cast<size_t>(N) * static_cast<size_t>(N));
    const int cells = (N - 1) * (N - 1);
    mesh.indices.reserve(static_cast<size_t>(cells) * 6u);

    // Centering offset (if requested)
    const float half = params.centerChunk ? (cs * (N - 1)) * 0.5f : 0.0f;

    // Height sampler with neighbor-aware border sampling.
    auto H = [&](int x, int y) -> float {
        if (x >= 0 && x < N && y >= 0 && y < N) {
            return chunk.height.at(x, y); // 0..1
        }
        if (neighbor) {
            return neighbor(x, y); // may read from adjacent chunk
        }
        // Safe fallback: clamp to edges
        x = std::clamp(x, 0, N - 1);
        y = std::clamp(y, 0, N - 1);
        return chunk.height.at(x, y);
    };

    // ---- 1) Build vertices (pos, normal, color, uv) ----
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            TerrainVertex v{};

            const float h = H(x, y) * hScale;

            v.px = x * cs - half + params.originX;
            v.py = h;
            v.pz = y * cs - half + params.originZ;

            if (params.generateNormals) {
                // Central differences with proper scaling to produce stable, seam-free normals.
                const float hL = H(x - 1, y);
                const float hR = H(x + 1, y);
                const float hD = H(x, y - 1);
                const float hU = H(x, y + 1);

                // Convert height delta (0..1) to meters with height scale,
                // then to slope by dividing by horizontal spacing (cs).
                const float sx = (hL - hR) * hScale / (2.0f * cs);
                const float sz = (hD - hU) * hScale / (2.0f * cs);

                // Normal pointing up:
                float nx = -sx, ny = 1.0f, nz = -sz;
                const float len2 = nx*nx + ny*ny + nz*nz;
                const float inv  = (len2 > 1e-16f) ? (1.0f / std::sqrt(len2)) : 1.0f;
                v.nx = nx * inv; v.ny = ny * inv; v.nz = nz * inv;
            } else {
                v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
            }

            // Color by biome id at the same sample
            const std::uint8_t biome = chunk.biome.at(x, y);
            v.rgba = biomeColor(biome);

            v.u = (N > 1) ? (static_cast<float>(x) / (N - 1)) : 0.0f;
            v.v = (N > 1) ? (static_cast<float>(y) / (N - 1)) : 0.0f;

            mesh.vertices.push_back(v);
        }
    }

    // ---- 2) Emit indices (two triangles per cell) ----
    auto idx = [N](int x, int y) -> std::uint32_t {
        return static_cast<std::uint32_t>(y * N + x);
    };

    const bool flip = params.flipWinding;

    for (int y = 0; y < N - 1; ++y) {
        for (int x = 0; x < N - 1; ++x) {
            const std::uint32_t i00 = idx(x,     y);
            const std::uint32_t i10 = idx(x + 1, y);
            const std::uint32_t i01 = idx(x,     y + 1);
            const std::uint32_t i11 = idx(x + 1, y + 1);

            if (!flip) {
                // CCW for Y-up
                mesh.indices.push_back(i00); mesh.indices.push_back(i11); mesh.indices.push_back(i10);
                mesh.indices.push_back(i00); mesh.indices.push_back(i01); mesh.indices.push_back(i11);
            } else {
                // CW
                mesh.indices.push_back(i00); mesh.indices.push_back(i10); mesh.indices.push_back(i11);
                mesh.indices.push_back(i00); mesh.indices.push_back(i11); mesh.indices.push_back(i01);
            }
        }
    }

    // ---- 3) Optional skirts to hide cracks (esp. with LOD) ----
    // If desired, add four edge strips extruded downward by params.skirtMeters.
    // (Not implemented here; left as an optional extension.)
    // if (params.skirtMeters > 0.0f) {
    //     // Add vertices/indices along each edge, offsetting Y by -params.skirtMeters.
    // }

    // Note on index size:
    // MeshData currently stores 32-bit indices. If you want to pack to 16-bit when possible,
    // you can do it later in the renderer when params.index32 == false and N*N <= 65535.

    return mesh;
}

} // namespace colony::worldgen
