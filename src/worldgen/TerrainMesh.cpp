#include "TerrainMesh.hpp"
#include <cmath>
#include <algorithm>
#include <cassert>

namespace colony::worldgen {

// -------------------- small math helpers --------------------
struct Vec3 { float x, y, z; };
static inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline Vec3& operator+=(Vec3& a, Vec3 b){ a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
static inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline Vec3 cross(Vec3 a, Vec3 b){
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static inline float dot(Vec3 a, Vec3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline Vec3 normalize(Vec3 v){
    float m2 = dot(v,v);
    if (m2 <= 1e-16f) return {0,1,0};
    float inv = 1.0f / std::sqrt(m2);
    return { v.x*inv, v.y*inv, v.z*inv };
}

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

MeshData BuildTerrainMesh(const WorldChunk& chunk, const TerrainMeshParams& params)
{
    MeshData mesh;

    const int N = chunk.height.width();
    assert(N == chunk.height.height() && "Height grid must be square");

    const float cell = params.cellSizeMeters;
    const float hScale = params.heightScale;

    // Vertex layout: one vertex per grid sample (N x N).
    mesh.vertices.resize(static_cast<size_t>(N) * N);

    // Optional centering so the chunk is around the origin.
    const float sizeX = (N - 1) * cell;
    const float sizeZ = (N - 1) * cell;
    const float baseX = params.centerChunk ? -0.5f * sizeX : params.originX;
    const float baseZ = params.centerChunk ? -0.5f * sizeZ : params.originZ;

    auto vIndex = [N](int x, int y) -> std::uint32_t {
        return static_cast<std::uint32_t>(y * N + x);
    };

    // ---- 1) Build positions, colors, UVs; zero normals (we'll accumulate) ----
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            TerrainVertex v{};
            const float h = chunk.height.at(x, y) * hScale;

            v.px = baseX + x * cell;
            v.py = h;
            v.pz = baseZ + y * cell;

            v.nx = 0.f; v.ny = 0.f; v.nz = 0.f; // will accumulate

            // Color by local biome id at the same sample
            const std::uint8_t biome = chunk.biome.at(x, y);
            v.rgba = biomeColor(biome);

            v.u = (N > 1) ? (static_cast<float>(x) / (N - 1)) : 0.f;
            v.v = (N > 1) ? (static_cast<float>(y) / (N - 1)) : 0.f;

            mesh.vertices[vIndex(x,y)] = v;
        }
    }

    // ---- 2) Emit indices and accumulate area-weighted face normals per vertex ----
    // Two triangles per cell. For Y-up, use CCW winding:
    // t0: (i00, i11, i10), t1: (i00, i01, i11).
    // (This yields upward-pointing normals for flat terrain.)
    const bool flip = params.flipWinding;

    mesh.indices.reserve(static_cast<size_t>(N-1) * (N-1) * 6);

    for (int y = 0; y < N-1; ++y) {
        for (int x = 0; x < N-1; ++x) {
            const std::uint32_t i00 = vIndex(x,   y);
            const std::uint32_t i10 = vIndex(x+1, y);
            const std::uint32_t i01 = vIndex(x,   y+1);
            const std::uint32_t i11 = vIndex(x+1, y+1);

            auto getP = [&](std::uint32_t i)->Vec3{
                const auto &v = mesh.vertices[i];
                return {v.px, v.py, v.pz};
            };

            auto accumulateTri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c){
                const Vec3 pa = getP(a), pb = getP(b), pc = getP(c);
                // Face normal via cross product of edges (area-weighted magnitude).
                // N = (pb - pa) x (pc - pa); add to all 3 vertices. (Normalize later)
                Vec3 n = cross( Vec3{pb.x-pa.x, pb.y-pa.y, pb.z-pa.z},
                                Vec3{pc.x-pa.x, pc.y-pa.y, pc.z-pa.z} );
                // Accumulate
                auto &va = mesh.vertices[a]; va.nx += n.x; va.ny += n.y; va.nz += n.z;
                auto &vb = mesh.vertices[b]; vb.nx += n.x; vb.ny += n.y; vb.nz += n.z;
                auto &vc = mesh.vertices[c]; vc.nx += n.x; vc.ny += n.y; vc.nz += n.z;

                mesh.indices.push_back(a);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
            };

            if (!flip) {
                accumulateTri(i00, i11, i10); // CCW
                accumulateTri(i00, i01, i11); // CCW
            } else {
                // Flip: CW
                accumulateTri(i00, i10, i11);
                accumulateTri(i00, i11, i01);
            }
        }
    }

    // ---- 3) Normalize all vertex normals ----
    for (auto &v : mesh.vertices) {
        Vec3 n = normalize(Vec3{v.nx, v.ny, v.nz});
        v.nx = n.x; v.ny = n.y; v.nz = n.z;
    }

    return mesh;
}

} // namespace colony::worldgen
