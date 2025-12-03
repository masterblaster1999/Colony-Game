#pragma once
// ============================================================================
// MarchingCubes_MCExtract.hpp — table-driven extractor (indexed mesh)
// Requires: worldgen/mc_tables.inl  and your VoxelVolume + MCParams structs
// ============================================================================

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#include "worldgen/mc_tables.inl"   // MC_EDGE_VERTS, MC_EDGE_TABLE, MC_TRI_TABLE

namespace worldgen {

struct Mesh {
    std::vector<float>     positions; // 3*N
    std::vector<float>     normals;   // 3*N (filled if compute_normals=true)
    std::vector<uint32_t>  indices;   // 3*M
    void clear(){ positions.clear(); normals.clear(); indices.clear(); }
    size_t vertex_count() const { return positions.size()/3; }
};

struct VoxelVolume {
    int W=0, H=0, Z=0;
    std::vector<uint8_t> vox;
    inline bool inb(int x,int y,int z) const {
        return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H && (unsigned)z<(unsigned)Z;
    }
    inline size_t idx(int x,int y,int z) const { return (size_t)z*(size_t)W*H + (size_t)y*(size_t)W + (size_t)x; }
    inline uint8_t get(int x,int y,int z) const { return inb(x,y,z) ? vox[idx(x,y,z)] : 0u; }
};

struct MCParams {
    float cell_size_x = 1.0f, cell_size_y = 1.0f, cell_size_z = 1.0f;
    float origin_x = 0.f, origin_y = 0.f, origin_z = 0.f;
    float iso = 0.5f;               // node isovalue
    bool  compute_normals = true;   // face-accumulated
    int   z0 = -1, z1 = -1;         // optional cell-z band
};

namespace detail {

struct V3 { float x,y,z; };
inline V3 operator+(const V3&a,const V3&b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline V3 operator-(const V3&a,const V3&b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline V3 operator*(const V3&a,float s){ return {a.x*s,a.y*s,a.z*s}; }
inline V3 cross(const V3&a,const V3&b){
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline float dot(const V3&a,const V3&b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
inline V3 normalize(const V3&v){
    float L = std::sqrt(std::max(1e-12f, dot(v,v)));
    return { v.x/L, v.y/L, v.z/L };
}

struct EdgeKeyHash {
    size_t operator()(const std::pair<uint32_t,uint32_t>& p) const noexcept {
        uint64_t a=p.first, b=p.second; if (a>b) std::swap(a,b);
        return (size_t)((a<<32)^b);
    }
};

// 8 cube corner offsets (node lattice): (x,y,z) in {0,1}^3
static constexpr int C8[8][3] = {
    {0,0,0},{1,0,0},{1,1,0},{0,1,0},
    {0,0,1},{1,0,1},{1,1,1},{0,1,1}
};

// Node lattice id on (W+1,H+1,Z+1)
inline uint32_t node_id(int nx,int ny,int nz, int W,int H,int Z){
    return (uint32_t)((nz*(H+1) + ny)*(W+1) + nx);
}

// Sample scalar at a lattice node by averaging the 8 adjacent cells (binary → [0..1]).
inline float sample_node_scalar(const VoxelVolume& vol, int nx,int ny,int nz){
    int xs[2] = {nx-1, nx}, ys[2] = {ny-1, ny}, zs[2] = {nz-1, nz};
    int cnt=0, sum=0;
    for(int k:zs) for(int j:ys) for(int i:xs){
        if (vol.inb(i,j,k)){ sum += (vol.get(i,j,k)?1:0); ++cnt; }
    }
    return cnt ? float(sum)/float(cnt) : 0.f;
}

// Interpolate along edge A->B (linear). Degenerate equal values → midpoint.
inline V3 interp(const V3& A,const V3& B, float vA,float vB,float iso){
    float t = std::fabs(vA-vB) < 1e-8f ? 0.5f : (iso - vA)/(vB - vA);
    if (t<0.f) t=0.f; else if (t>1.f) t=1.f;
    return A*(1.f - t) + B*t;
}

// Accumulate a face normal into 3 vertex normals
inline void add_face_normal(Mesh& M, uint32_t i0, uint32_t i1, uint32_t i2){
    V3 p0{M.positions[3*i0+0], M.positions[3*i0+1], M.positions[3*i0+2]};
    V3 p1{M.positions[3*i1+0], M.positions[3*i1+1], M.positions[3*i1+2]};
    V3 p2{M.positions[3*i2+0], M.positions[3*i2+1], M.positions[3*i2+2]};
    V3 n = cross(p1-p0, p2-p0);
    for (uint32_t i : {i0,i1,i2}){
        M.normals[3*i+0]+=n.x; M.normals[3*i+1]+=n.y; M.normals[3*i+2]+=n.z;
    }
}

} // namespace detail

// -------------------- Table-driven extractor (MC) ----------------------------
// Same input/output shape as the stub. If you prefer the same *name* as the
// stub function, you can `#define BuildMeshFromVoxelVolume BuildMeshFromVoxelVolume_MC`
// before including this header.
inline Mesh BuildMeshFromVoxelVolume_MC(const VoxelVolume& vol, const MCParams& P)
{
    using namespace detail;
    using namespace mc_tables;

    Mesh M; M.clear();
    M.positions.reserve((size_t)vol.W*vol.H*vol.Z);
    if (P.compute_normals) M.normals.reserve((size_t)vol.W*vol.H*vol.Z);
    M.indices.reserve((size_t)vol.W*vol.H*vol.Z*6);

    // Global per-lattice-edge vertex cache: (nodeA,nodeB) -> vertex index
    std::unordered_map<std::pair<uint32_t,uint32_t>, uint32_t, EdgeKeyHash> Vmap;
    Vmap.reserve((size_t)vol.W*vol.H*6);

    auto node_world = [&](int nx,int ny,int nz)->V3{
        return { P.origin_x + nx*P.cell_size_x,
                 P.origin_y + ny*P.cell_size_y,
                 P.origin_z + nz*P.cell_size_z };
    };

    // Working arrays per cube
    V3 edgePos[12];
    uint32_t edgeIdx[12] = {0};

    int zc0 = (P.z0>=0 ? std::max(0,P.z0) : 0);
    int zc1 = (P.z1>=0 ? std::min(vol.Z-1,P.z1) : vol.Z-1);

    for (int cz=zc0; cz<zc1; ++cz)
    for (int cy=0; cy<vol.H-1; ++cy)
    for (int cx=0; cx<vol.W-1; ++cx)
    {
        // Sample node values at the 8 lattice corners of this cube
        int NX[8], NY[8], NZ[8];
        float S[8]; V3 P8[8];
        for (int i=0;i<8;++i){
            NX[i]=cx+C8[i][0]; NY[i]=cy+C8[i][1]; NZ[i]=cz+C8[i][2];
            S[i]=sample_node_scalar(vol,NX[i],NY[i],NZ[i]);
            P8[i]=node_world(NX[i],NY[i],NZ[i]);
        }

        // Build cube case index and edge mask
        int cubeIndex = 0;
        for (int i=0;i<8;++i) if (S[i] >= P.iso) cubeIndex |= (1<<i);
        uint16_t mask = MC_EDGE_TABLE[cubeIndex];
        if (!mask) continue;

        // Intersections on the 12 edges, with vertex dedup keyed by lattice edge
        for (int e=0;e<12;++e){
            if (!(mask & (1u<<e))) continue;
            int a = MC_EDGE_VERTS[e][0], b = MC_EDGE_VERTS[e][1];

            // Dedup key: pair of lattice node ids (sorted)
            uint32_t Aid = node_id(NX[a],NY[a],NZ[a], vol.W,vol.H,vol.Z);
            uint32_t Bid = node_id(NX[b],NY[b],NZ[b], vol.W,vol.H,vol.Z);
            if (Aid>Bid) std::swap(Aid,Bid);
            auto key = std::make_pair(Aid,Bid);

            auto it = Vmap.find(key);
            if (it != Vmap.end()){
                edgeIdx[e] = it->second;
            } else {
                V3 pos = interp(P8[a], P8[b], S[a], S[b], P.iso);
                uint32_t idx = (uint32_t)(M.positions.size()/3);
                M.positions.insert(M.positions.end(), {pos.x,pos.y,pos.z});
                if (P.compute_normals) M.normals.insert(M.normals.end(), {0.f,0.f,0.f});
                Vmap.emplace(key, idx);
                edgeIdx[e] = idx;
            }
        }

        // Emit triangles using the table for this case
        const int8_t* tri = MC_TRI_TABLE[cubeIndex];
        for (int t=0; tri[t] != -1; t += 3){
            uint32_t i0 = edgeIdx[(int)tri[t+0]];
            uint32_t i1 = edgeIdx[(int)tri[t+1]];
            uint32_t i2 = edgeIdx[(int)tri[t+2]];
            M.indices.push_back(i0); M.indices.push_back(i1); M.indices.push_back(i2);
            if (P.compute_normals) add_face_normal(M, i0,i1,i2);
        }
    }

    // Normalize accumulated normals
    if (P.compute_normals && !M.normals.empty()){
        for (size_t i=0;i<M.vertex_count();++i){
            V3 n{M.normals[3*i+0],M.normals[3*i+1],M.normals[3*i+2]};
            n = normalize(n);
            M.normals[3*i+0]=n.x; M.normals[3*i+1]=n.y; M.normals[3*i+2]=n.z;
        }
    }
    return M;
}

} // namespace worldgen
