#pragma once
// ============================================================================
// MarchingCubesStub.hpp — tiny isosurface extractor (indexed triangles)
// C++17 / STL-only | Designed to work with vox::VoxelVolume from your helpers.
//
// Approach:
//   • "Marching Cubes" via a 6‑tetrahedra decomposition of each cube.
//   • No giant 256‑case tables: each tetra has only 16 cases; we handle them
//     procedurally (1‑tri or 2‑tri) with linear edge interpolation.
//   • Input: vox::VoxelVolume (uint8_t occupancy; 1=solid, 0=empty) + iso.
//   • Output: TriangleMesh (positions, normals, indices).
//
// References (concepts; implementation here is original):
//   - Lorensen & Cline, *Marching Cubes* (SIGGRAPH’87).  [classic MC]    [see cites]
//   - Paul Bourke’s notes and tables for MC & Tetrahedra.                 [see cites]
//   - Marching Tetrahedra overview (ambiguity‑free alternative).          [see cites]
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace vox {

// If you already defined these in another header, feel free to reuse them.
struct VoxelVolume {
    int nx=0, ny=0, nz=0;     // grid size
    float cell=1.0f;          // world units per voxel
    std::vector<uint8_t> v;   // size nx*ny*nz; 1=solid, 0=empty
    inline bool inb(int x,int y,int z) const {
        return (unsigned)x<(unsigned)nx && (unsigned)y<(unsigned)ny && (unsigned)z<(unsigned)nz;
    }
    inline size_t I(int x,int y,int z) const { return (size_t)z*(size_t)nx*(size_t)ny + (size_t)y*(size_t)nx + (size_t)x; }
    inline uint8_t at(int x,int y,int z) const { return v[I(x,y,z)]; }
};

} // namespace vox

namespace mc {

struct float3 { float x=0,y=0,z=0; };
struct TriangleMesh {
    std::vector<float3> positions;
    std::vector<float3> normals;
    std::vector<uint32_t> indices;   // 3 per triangle
};

static inline float3 make3(float x,float y,float z){ return {x,y,z}; }
static inline float3 add(const float3&a,const float3&b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline float3 sub(const float3&a,const float3&b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline float3 mul(const float3&a,float s){ return {a.x*s,a.y*s,a.z*s}; }
static inline float  dot(const float3&a,const float3&b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline float3 cross(const float3&a,const float3&b){ return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static inline float  length(const float3&a){ return std::sqrt(dot(a,a)); }
static inline float3 normalize(const float3&a){ float L=length(a); return L>1e-12f? float3{a.x/L,a.y/L,a.z/L}:float3{0,0,0}; }

// Linear edge interpolation
static inline float3 lerpEdge(const float3& p0,const float3& p1,float v0,float v1,float iso){
    float t;
    if (std::fabs(v1 - v0) < 1e-8f) t = 0.5f;
    else                            t = (iso - v0) / (v1 - v0);
    t = std::clamp(t, 0.0f, 1.0f);
    return add(p0, mul(sub(p1,p0), t));
}

// ---- Marching "Cubes" via 6‑tet decomposition --------------------------------
// Each cube cell is split into 6 tets that share the main diagonal (0→6).
// Corner numbering matches the usual MC convention:
//    0:(0,0,0) 1:(1,0,0) 2:(1,1,0) 3:(0,1,0)
//    4:(0,0,1) 5:(1,0,1) 6:(1,1,1) 7:(0,1,1)
// (This is the common split; any consistent 6‑tet split works.)  See refs.
// ------------------------------------------------------------------------------
static constexpr int TETS[6][4] = {
    {0,5,1,6}, {0,1,2,6}, {0,2,3,6}, {0,3,7,6}, {0,7,4,6}, {0,4,5,6}
};

// For one tetra {a,b,c,d}, return the triangles for the iso (0..2 tris).
// val[] are corner scalars; pos[] are world positions of the 4 corners.
// "inside" is defined by (solidHigh? v>iso : v<iso).
template<bool solidHigh>
static inline void emitTetra(const float val[4], const float3 pos[4],
                             float iso, std::vector<float3>& P, std::vector<uint32_t>& I)
{
    auto isInside = [&](int idx)->bool{ return solidHigh ? (val[idx] > iso) : (val[idx] < iso); };

    // mark corners
    int inMask = 0;
    for (int i=0;i<4;++i) if (isInside(i)) inMask |= (1<<i);
    if (inMask==0 || inMask==0xF) return; // fully out or in → no surface

    // Helper: intersection point on edge (i,j)
    auto vtx = [&](int i,int j)->float3{
        return lerpEdge(pos[i], pos[j], val[i], val[j], iso);
    };

    // Count bits (inside corners)
    auto pop = [](int x){ x = (x&0x55)+( (x>>1)&0x55 );
                          x = (x&0x33)+( (x>>2)&0x33 );
                          return (x&0x0F); };
    int n = pop(inMask);

    auto pushTri = [&](const float3& a,const float3& b,const float3& c){
        uint32_t base = (uint32_t)P.size();
        P.push_back(a); P.push_back(b); P.push_back(c);
        I.push_back(base+0); I.push_back(base+1); I.push_back(base+2);
    };

    // Case A: 1‑in / 3‑out  (or 3‑in / 1‑out, complement)
    if (n==1 || n==3){
        // pick the "special" corner: inside for n==1, outside for n==3
        int s = (n==1) ? __builtin_ctz(inMask) : __builtin_ctz((~inMask)&0xF);
        // list the other three corners
        int others[3], k=0;
        for(int i=0;i<4;++i) if (i!=s) others[k++]=i;

        // Build triangle around the special corner
        // If n==1, edges go (s,others[i]); if n==3, use the outside corner instead
        // The orientation (winding) can be flipped later if you want outward normals.
        float3 a = vtx(s, others[0]);
        float3 b = vtx(s, others[1]);
        float3 c = vtx(s, others[2]);

        // For the 3‑in case, flipping winding keeps outward normals consistent
        if (n==3) pushTri(a,c,b); else pushTri(a,b,c);
        return;
    }

    // Case B: 2‑in / 2‑out → quad split into two triangles.
    // Collect indices of inside and outside corners
    int inside[2], outside[2], ii=0, oo=0;
    for(int i=0;i<4;++i) ( (inMask>>i)&1 ? inside[ii++]=i : outside[oo++]=i );

    // The surface intersects the four edges (inside↔outside)
    float3 p00 = vtx(inside[0], outside[0]);
    float3 p01 = vtx(inside[0], outside[1]);
    float3 p10 = vtx(inside[1], outside[0]);
    float3 p11 = vtx(inside[1], outside[1]);

    // Emit two tris; pick a consistent diagonal
    pushTri(p00, p10, p11);
    pushTri(p00, p11, p01);
}

// ---- Main extractor ----------------------------------------------------------

struct Options {
    float iso = 0.5f;          // isovalue
    bool  solidHigh = true;    // VoxelVolume stores solids as 1.0 (above iso)
    bool  computeNormals = true;
};

inline TriangleMesh extract(const vox::VoxelVolume& V, const Options& opt = {})
{
    TriangleMesh M;
    const int X = V.nx, Y = V.ny, Z = V.nz;
    if (X<2 || Y<2 || Z<2 || V.v.empty()) return M;

    M.positions.reserve((size_t)X*Y*Z/2); // rough guess
    M.indices.reserve((size_t)X*Y*Z);

    const float cs = V.cell;

    // Iterate all cube cells
    for (int z=0; z<Z-1; ++z){
        for (int y=0; y<Y-1; ++y){
            for (int x=0; x<X-1; ++x){
                // Corner positions (world) and scalar values (float)
                float3 P8[8] = {
                    { (x+0)*cs, (y+0)*cs, (z+0)*cs }, // 0
                    { (x+1)*cs, (y+0)*cs, (z+0)*cs }, // 1
                    { (x+1)*cs, (y+1)*cs, (z+0)*cs }, // 2
                    { (x+0)*cs, (y+1)*cs, (z+0)*cs }, // 3
                    { (x+0)*cs, (y+0)*cs, (z+1)*cs }, // 4
                    { (x+1)*cs, (y+0)*cs, (z+1)*cs }, // 5
                    { (x+1)*cs, (y+1)*cs, (z+1)*cs }, // 6
                    { (x+0)*cs, (y+1)*cs, (z+1)*cs }  // 7
                };
                float S8[8] = {
                    float(V.at(x  ,y  ,z  )), float(V.at(x+1,y  ,z  )),
                    float(V.at(x+1,y+1,z  )), float(V.at(x  ,y+1,z  )),
                    float(V.at(x  ,y  ,z+1)), float(V.at(x+1,y  ,z+1)),
                    float(V.at(x+1,y+1,z+1)), float(V.at(x  ,y+1,z+1))
                };

                // Process 6 tetrahedra
                for (int t=0; t<6; ++t){
                    const int a=TETS[t][0], b=TETS[t][1], c=TETS[t][2], d=TETS[t][3];
                    float  val[4] = { S8[a], S8[b], S8[c], S8[d] };
                    float3 pos[4] = { P8[a], P8[b], P8[c], P8[d] };
                    if (opt.solidHigh) emitTetra<true >(val, pos, opt.iso, M.positions, M.indices);
                    else               emitTetra<false>(val, pos, opt.iso, M.positions, M.indices);
                }
            }
        }
    }

    // Normals (area‑weighted from faces), if requested
    M.normals.assign(M.positions.size(), float3{0,0,0});
    if (opt.computeNormals){
        for (size_t i=0; i<M.indices.size(); i+=3){
            uint32_t ia = M.indices[i+0], ib = M.indices[i+1], ic = M.indices[i+2];
            const float3& A = M.positions[ia];
            const float3& B = M.positions[ib];
            const float3& C = M.positions[ic];
            float3 n = cross( sub(B,A), sub(C,A) );
            M.normals[ia] = add(M.normals[ia], n);
            M.normals[ib] = add(M.normals[ib], n);
            M.normals[ic] = add(M.normals[ic], n);
        }
        for (auto& n : M.normals) n = normalize(n);
    }

    return M;
}

} // namespace mc
