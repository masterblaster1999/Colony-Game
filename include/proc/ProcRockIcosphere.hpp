// include/proc/ProcRockIcosphere.hpp
// Header-only: generate a low-poly "rock" by subdividing an icosahedron and
// displacing vertices along normals with domain-warped fBm noise.
//
// References / motivation:
// - Icosphere subdivision gives uniform triangles vs UV sphere. (even vertex distribution)
// - Displacement via fBm + domain warping adds natural facets/strata.
// See: Daniel Sieger’s “Generating spheres – the icosphere”, and general icosphere resources. 

#pragma once
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <limits>

namespace proc
{
    struct Float3 { float x,y,z; };
    struct Vertex  { Float3 pos; Float3 nrm; };

    struct Mesh {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    // ---------- small math ----------
    inline Float3 make3(float x,float y,float z){ return {x,y,z}; }
    inline Float3 add(const Float3&a,const Float3&b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
    inline Float3 sub(const Float3&a,const Float3&b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
    inline Float3 mul(const Float3&a,float s){ return {a.x*s,a.y*s,a.z*s}; }
    inline float  dot(const Float3&a,const Float3&b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
    inline Float3 cross(const Float3&a,const Float3&b){
        return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
    }
    inline float  length(const Float3&a){ return std::sqrt(dot(a,a)); }
    inline Float3 normalize(const Float3&a){
        float L=length(a); return (L>0)? make3(a.x/L,a.y/L,a.z/L): make3(0,1,0);
    }

    // ---------- hashing for midpoint cache ----------
    struct Edge { uint32_t a,b; };
    struct EdgeKey {
        uint32_t a,b;
        bool operator==(const EdgeKey& o) const { return a==o.a && b==o.b; }
    };
    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& k) const noexcept {
            return (size_t)k.a * 73856093u ^ (size_t)k.b * 19349663u;
        }
    };

    // ---------- noise (value noise 3D + fBm + simple warp) ----------
    inline float hash31(float x, float y, float z){
        float n = x*127.1f + y*311.7f + z*74.7f;
        return std::fmod(std::sin(n)*43758.5453123f, 1.0f);
    }
    inline float lerp(float a,float b,float t){ return a + (b-a)*t; }
    inline float smooth(float t){ return t*t*(3.0f-2.0f*t); }

    inline float valueNoise3D(float x, float y, float z){
        float ix = std::floor(x), fx = x-ix;
        float iy = std::floor(y), fy = y-iy;
        float iz = std::floor(z), fz = z-iz;

        float n000 = hash31(ix+0,iy+0,iz+0);
        float n100 = hash31(ix+1,iy+0,iz+0);
        float n010 = hash31(ix+0,iy+1,iz+0);
        float n110 = hash31(ix+1,iy+1,iz+0);
        float n001 = hash31(ix+0,iy+0,iz+1);
        float n101 = hash31(ix+1,iy+0,iz+1);
        float n011 = hash31(ix+0,iy+1,iz+1);
        float n111 = hash31(ix+1,iy+1,iz+1);

        float ux = smooth(fx), uy = smooth(fy), uz = smooth(fz);
        float nx00 = lerp(n000,n100,ux);
        float nx10 = lerp(n010,n110,ux);
        float nx01 = lerp(n001,n101,ux);
        float nx11 = lerp(n011,n111,ux);
        float nxy0 = lerp(nx00,nx10,uy);
        float nxy1 = lerp(nx01,nx11,uy);
        return lerp(nxy0,nxy1,uz);
    }

    inline float fbm3D(float x, float y, float z, int octaves, float baseFreq, float lacun, float gain){
        float v=0.0f, amp=0.5f, freq=baseFreq;
        for(int i=0;i<octaves;i++){
            v += amp * valueNoise3D(x*freq, y*freq, z*freq);
            freq *= lacun;
            amp  *= gain;
        }
        return v;
    }

    // ---------- icosahedron base ----------
    inline void makeIcosahedron(std::vector<Float3>& positions, std::vector<uint32_t>& indices)
    {
        positions.clear(); indices.clear();
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f; // golden ratio
        // 12 vertices (unnormalized)
        positions = {
            make3(-1,  t,  0), make3( 1,  t,  0), make3(-1, -t,  0), make3( 1, -t,  0),
            make3( 0, -1,  t), make3( 0,  1,  t), make3( 0, -1, -t), make3( 0,  1, -t),
            make3( t,  0, -1), make3( t,  0,  1), make3(-t, 0, -1), make3(-t, 0,  1)
        };
        for(auto& p: positions) p = normalize(p); // unit radius

        // 20 faces
        uint32_t idx[] = {
            0,11,5,  0,5,1,   0,1,7,   0,7,10,  0,10,11,
            1,5,9,   5,11,4,  11,10,2, 10,7,6,  7,1,8,
            3,9,4,   3,4,2,   3,2,6,   3,6,8,   3,8,9,
            4,9,5,   2,4,11,  6,2,10,  8,6,7,   9,8,1
        };
        indices.assign(std::begin(idx), std::end(idx));
    }

    inline uint32_t midpoint(std::vector<Float3>& vtx,
                             std::unordered_map<EdgeKey,uint32_t,EdgeKeyHash>& cache,
                             uint32_t a, uint32_t b)
    {
        EdgeKey key{ a < b ? a : b, a < b ? b : a };
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        Float3 p = normalize( mul( add(vtx[a], vtx[b]), 0.5f ) );
        uint32_t id = (uint32_t)vtx.size();
        vtx.push_back(p);
        cache.emplace(key, id);
        return id;
    }

    inline void subdivideIcosphere(std::vector<Float3>& vtx, std::vector<uint32_t>& idx, int levels)
    {
        for(int l=0;l<levels;l++){
            std::unordered_map<EdgeKey,uint32_t,EdgeKeyHash> cache;
            std::vector<uint32_t> next;
            next.reserve(idx.size()*4);
            for(size_t t=0;t<idx.size(); t+=3){
                uint32_t i0=idx[t+0], i1=idx[t+1], i2=idx[t+2];
                uint32_t a = midpoint(vtx,cache,i0,i1);
                uint32_t b = midpoint(vtx,cache,i1,i2);
                uint32_t c = midpoint(vtx,cache,i2,i0);
                // 4 new faces
                next.insert(next.end(), { i0,a,c,  i1,b,a,  i2,c,b,  a,b,c });
            }
            idx.swap(next);
        }
    }

    inline void computeNormals(const std::vector<Float3>& pos, const std::vector<uint32_t>& idx,
                               std::vector<Float3>& outNrm)
    {
        outNrm.assign(pos.size(), make3(0,0,0));
        for(size_t i=0;i<idx.size();i+=3){
            uint32_t i0=idx[i+0], i1=idx[i+1], i2=idx[i+2];
            Float3 e1 = sub(pos[i1],pos[i0]);
            Float3 e2 = sub(pos[i2],pos[i0]);
            Float3 n  = normalize(cross(e1,e2));
            outNrm[i0]= add(outNrm[i0],n);
            outNrm[i1]= add(outNrm[i1],n);
            outNrm[i2]= add(outNrm[i2],n);
        }
        for(size_t i=0;i<outNrm.size();++i) outNrm[i]=normalize(outNrm[i]);
    }

    // --------- Public API ----------
    struct RockParams {
        float radius        = 1.0f;   // base radius
        int   subdivisions  = 2;      // 0..4 typical
        int   octaves       = 4;      // fBm octaves
        float baseFreq      = 1.5f;   // noise freq on unit sphere
        float lacunarity    = 2.0f;
        float gain          = 0.5f;
        float warpStrength  = 0.35f;  // domain warp along tangent
        float dispAmplitude = 0.35f;  // displacement along normal (as fraction of radius)
        uint32_t seed       = 1337u;  // (seed only affects phase if you add it to noise)
    };

    inline Mesh GenerateRockMesh(const RockParams& P)
    {
        // 1) Icosahedron -> subdivided icosphere
        std::vector<Float3> spherePos;
        std::vector<uint32_t> tri;
        makeIcosahedron(spherePos, tri);
        subdivideIcosphere(spherePos, tri, P.subdivisions);

        // 2) Displace along normal with domain-warped fBm
        std::vector<Float3> nrm;
        computeNormals(spherePos, tri, nrm);

        // simple tangent-ish warp: project a small 3D warp onto local frame
        for (size_t i=0;i<spherePos.size();++i){
            Float3 p  = spherePos[i];
            Float3 n  = nrm[i];

            // Build a local frame (n, t1, t2)
            Float3 up = (std::fabs(n.y) < 0.99f) ? make3(0,1,0) : make3(1,0,0);
            Float3 t1 = normalize(cross(up, n));
            Float3 t2 = cross(n, t1);

            // Domain warp in tangent plane
            float wx = valueNoise3D(dot(t1,p)+11.0f, dot(t2,p)+23.0f, dot(n,p)+37.0f);
            float wy = valueNoise3D(dot(t1,p)+41.0f, dot(t2,p)+53.0f, dot(n,p)+67.0f);
            Float3 warp = add( mul(t1, (wx*2.f-1.f) * P.warpStrength ),
                               mul(t2, (wy*2.f-1.f) * P.warpStrength ) );

            Float3 pp = add(p, warp);

            float f = fbm3D(pp.x + (float)P.seed*0.01f,
                            pp.y + (float)P.seed*0.02f,
                            pp.z + (float)P.seed*0.03f,
                            P.octaves, P.baseFreq, P.lacunarity, P.gain);

            float disp = 1.0f + (f*2.f-1.f) * P.dispAmplitude; // [-amp,+amp] around 1
            spherePos[i] = mul( normalize(p), P.radius * disp );
        }

        // 3) Recompute smooth normals
        computeNormals(spherePos, tri, nrm);

        // 4) Pack mesh
        Mesh m;
        m.vertices.resize(spherePos.size());
        for(size_t i=0;i<spherePos.size();++i){
            m.vertices[i].pos = spherePos[i];
            m.vertices[i].nrm = nrm[i];
        }
        m.indices = tri;
        return m;
    }
} // namespace proc
