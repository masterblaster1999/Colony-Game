#include "TerrainMesh.hpp"
#include <algorithm>
#include <cmath>

namespace cg {

static inline void biomeColor(uint8_t b, float& r,float& g,float& bl,float& a) {
    // Tweak to taste; gives quick visual feedback
    switch (static_cast<pg::Biome>(b)) {
        case pg::Biome::Ocean:           r=0.10f; g=0.25f; bl=0.70f; a=1; break;
        case pg::Biome::Beach:           r=0.88f; g=0.80f; bl=0.55f; a=1; break;
        case pg::Biome::Desert:          r=0.93f; g=0.82f; bl=0.46f; a=1; break;
        case pg::Biome::Savanna:         r=0.65f; g=0.72f; bl=0.25f; a=1; break;
        case pg::Biome::Grassland:       r=0.25f; g=0.65f; bl=0.30f; a=1; break;
        case pg::Biome::Shrubland:       r=0.50f; g=0.60f; bl=0.40f; a=1; break;
        case pg::Biome::TemperateForest: r=0.20f; g=0.55f; bl=0.25f; a=1; break;
        case pg::Biome::BorealForest:    r=0.15f; g=0.40f; bl=0.20f; a=1; break;
        case pg::Biome::TropicalForest:  r=0.10f; g=0.60f; bl=0.30f; a=1; break;
        case pg::Biome::Tundra:          r=0.70f; g=0.75f; bl=0.80f; a=1; break;
        case pg::Biome::Bare: default:   r=0.50f; g=0.50f; bl=0.50f; a=1; break;
    }
}

static inline void normalAt(const pg::Map2D& H, int x,int y, float zScale,
                            float& nx,float& ny,float& nz) {
    auto clampi = [&](int v,int a,int b){ return (v<a)?a:((v>b)?b:v); };
    int x0 = clampi(x-1, 0, H.w-1), x1 = clampi(x+1, 0, H.w-1);
    int y0 = clampi(y-1, 0, H.h-1), y1 = clampi(y+1, 0, H.h-1);
    float sx = (H.at(x1,y) - H.at(x0,y)) * 0.5f * zScale;
    float sy = (H.at(x,y1) - H.at(x,y0)) * 0.5f * zScale;
    // build normal from gradient (assuming +y down in texture space)
    nx = -sx; ny = -sy; nz = 1.0f;
    float len = std::sqrt(nx*nx + ny*ny + nz*nz) + 1e-8f;
    nx/=len; ny/=len; nz/=len;
}

TerrainMeshData BuildTerrainMesh(const pg::Outputs& W, float xyScale, float zScale) {
    const int Ww = W.height.w, Wh = W.height.h;
    TerrainMeshData M;
    M.vertices.resize(size_t(Ww)*Wh);

    // Vertices
    for (int y=0; y<Wh; ++y) {
        for (int x=0; x<Ww; ++x) {
            auto& v = M.vertices[size_t(y)*Ww + x];
            v.px = x * xyScale;
            v.py = W.height.at(x,y) * zScale;
            v.pz = y * xyScale;

            normalAt(W.height, x,y, zScale, v.nx,v.ny,v.nz);

            float r,g,b,a;
            biomeColor(W.biomes.at(x,y), r,g,b,a);

            // Optional water overlay (rivers / lakes) for quick visual feedback.
            if (W.water.w == Ww && W.water.h == Wh) {
                const auto wk = static_cast<pg::WaterKind>(W.water.at(x,y));
                if (wk == pg::WaterKind::River) {
                    r = r * 0.15f + 0.10f;
                    g = g * 0.15f + 0.35f;
                    b = b * 0.15f + 0.95f;
                } else if (wk == pg::WaterKind::Lake) {
                    r = r * 0.10f + 0.08f;
                    g = g * 0.10f + 0.28f;
                    b = b * 0.10f + 0.85f;
                }
            }

            v.r=r; v.g=g; v.b=b; v.a=a;
        }
    }

    // Indices (two triangles per cell)
    M.indices.reserve(size_t(Ww-1)* (Wh-1) * 6);
    for (int y=0; y<Wh-1; ++y) {
        for (int x=0; x<Ww-1; ++x) {
            uint32_t i0 = uint32_t(y*Ww + x);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + Ww;
            uint32_t i3 = i2 + 1;
            // tri 1
            M.indices.push_back(i0); M.indices.push_back(i2); M.indices.push_back(i1);
            // tri 2
            M.indices.push_back(i1); M.indices.push_back(i2); M.indices.push_back(i3);
        }
    }
    return M;
}

} // namespace cg
