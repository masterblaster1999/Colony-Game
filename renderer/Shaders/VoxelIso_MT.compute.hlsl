// renderer/Shaders/VoxelIso_MT.compute.hlsl
// Voxel isosurface extraction using Marching Tetrahedra (MT). Outputs a triangle vertex stream.
// Keep it simple: no index buffer, we append 3 vertices per triangle.
// References: split cube into 6 tets; 16 MT cases; avoids MC ambiguity and large tables.

#ifndef MT_THREADS_X
#define MT_THREADS_X 4
#endif
#ifndef MT_THREADS_Y
#define MT_THREADS_Y 4
#endif
#ifndef MT_THREADS_Z
#define MT_THREADS_Z 4
#endif

struct Vert {
    float3 pos;
    float3 nrm;
};

cbuffer VoxelCB : register(b0)
{
    uint3 VolumeSize;      // cells along X,Y,Z (number of cubes = VolumeSize-1)
    float  CellSize;       // voxel cell size in world units
    float3 OriginWS;       // world-space origin for (0,0,0)
    float  IsoValue;       // isosurface threshold (typically 0)
    uint   Seed;           // noise seed
    float  NoiseFreq;      // frequency in cycles/meter
    float  NoiseGain;      // fbm gain (~0.5)
    float  NoiseLacun;     // fbm lacunarity (~2.0)
    int    NoiseOctaves;   // 3..6 typical
    float  WarpStrength;   // domain-warp strength (meters)
    float  GradDelta;      // gradient epsilon (meters) for normals
};

AppendStructuredBuffer<Vert> OutVerts : register(u0);

// ---------- Noise (3D value noise + domain warp) ----------
float hash31(float3 p) {
    return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453123);
}
float valueNoise3D(float3 p) {
    float3 i = floor(p), f = frac(p);
    float n000 = hash31(i + float3(0,0,0));
    float n100 = hash31(i + float3(1,0,0));
    float n010 = hash31(i + float3(0,1,0));
    float n110 = hash31(i + float3(1,1,0));
    float n001 = hash31(i + float3(0,0,1));
    float n101 = hash31(i + float3(1,0,1));
    float n011 = hash31(i + float3(0,1,1));
    float n111 = hash31(i + float3(1,1,1));
    float3 u = f * f * (3.0 - 2.0 * f);
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

float fbm3D(float3 p) {
    float amp = 0.5;
    float freq = NoiseFreq;
    float v = 0.0;
    [unroll]
    for (int i = 0; i < 12; ++i) {
        if (i >= NoiseOctaves) break;
        v += amp * valueNoise3D(p * freq);
        freq *= NoiseLacun;
        amp  *= NoiseGain;
    }
    return v;
}

float densityField(float3 p) {
    // Domain warp: offset p by a low-frequency vector noise
    float3 pw = p * (NoiseFreq * 0.35) + Seed * 0.137;
    float3 warp = float3(valueNoise3D(pw + 11.0),
                         valueNoise3D(pw + 23.0),
                         valueNoise3D(pw + 47.0));
    warp = (warp * 2.0 - 1.0) * WarpStrength;
    float val = fbm3D(p + warp);
    // Center around 0 so IsoValue=0 creates tunnels/caves
    return (val - 0.5);
}

float3 densityGrad(float3 p) {
    float h = GradDelta;
    float dx = densityField(p + float3(h,0,0)) - densityField(p - float3(h,0,0));
    float dy = densityField(p + float3(0,h,0)) - densityField(p - float3(0,h,0));
    float dz = densityField(p + float3(0,0,h)) - densityField(p - float3(0,0,h));
    float3 g = float3(dx,dy,dz) / (2.0 * h);
    return g;
}

float3 interp(float iso, float3 p0, float3 p1, float d0, float d1) {
    float t = (iso - d0) / (d1 - d0);
    t = saturate(t);
    return lerp(p0, p1, t);
}

// ---- Marching Tetrahedra setup ----
// 6 tetrahedra splitting a cube (see NYU / Perlin’s class notes)
static const uint4 kTets[6] = {
    uint4(0,1,2,7), uint4(0,1,5,7), uint4(0,2,3,7),
    uint4(0,2,6,7), uint4(0,4,5,7), uint4(0,4,6,7)
};

// Tetra edge -> (vA,vB) in local tet-vertex indices [0..3]
static const uint2 kTetEdge[6] = {
    uint2(0,1), uint2(1,2), uint2(2,0),
    uint2(0,3), uint2(1,3), uint2(2,3)
};

// 16 MT cases -> up to 2 triangles -> 6 edge ids; -1 terminator
static const int kMTri[16][7] = {
    {-1,-1,-1,-1,-1,-1,-1},                 // 0000
    { 0, 3, 2, -1,-1,-1,-1},                 // 0001
    { 0, 1, 4, -1,-1,-1,-1},                 // 0010
    { 1, 4, 2, 2, 4, 3, -1},                 // 0011
    { 1, 2, 5, -1,-1,-1,-1},                 // 0100
    { 0, 3, 5, 0, 5, 1, -1},                 // 0101
    { 0, 2, 5, 0, 5, 4, -1},                 // 0110
    { 3, 5, 4, -1,-1,-1,-1},                 // 0111
    { 3, 4, 5, -1,-1,-1,-1},                 // 1000
    { 0, 4, 5, 0, 5, 2, -1},                 // 1001
    { 0, 5, 1, 0, 3, 5, -1},                 // 1010
    { 1, 5, 2, -1,-1,-1,-1},                 // 1011
    { 1, 3, 4, 1, 2, 3, -1},                 // 1100
    { 0, 4, 1, -1,-1,-1,-1},                 // 1101
    { 0, 2, 3, -1,-1,-1,-1},                 // 1110
    {-1,-1,-1,-1,-1,-1,-1}                  // 1111
};

[numthreads(MT_THREADS_X, MT_THREADS_Y, MT_THREADS_Z)]
void CSMain(uint3 gtid : SV_DispatchThreadID)
{
    // Process one cube per thread
    if (gtid.x >= VolumeSize.x - 1 || gtid.y >= VolumeSize.y - 1 || gtid.z >= VolumeSize.z - 1) return;

    // Cube corner positions (Perlin’s indexing)
    float3 base = OriginWS + (float3(gtid) * CellSize);
    float3 P[8];
    P[0] = base + float3(0,0,0);
    P[1] = base + float3(CellSize,0,0);
    P[2] = base + float3(0,CellSize,0);
    P[3] = base + float3(CellSize,CellSize,0);
    P[4] = base + float3(0,0,CellSize);
    P[5] = base + float3(CellSize,0,CellSize);
    P[6] = base + float3(0,CellSize,CellSize);
    P[7] = base + float3(CellSize,CellSize,CellSize);

    // Precompute densities at the 8 cube corners (world coords in meters)
    float d[8];
    [unroll] for (int i=0;i<8;i++) d[i] = densityField(P[i]);

    // March 6 tetrahedra
    [unroll] for (int t=0;t<6;t++) {
        uint4 tv = kTets[t]; // indices into the cube’s 8 corners
        float td[4] = { d[tv.x], d[tv.y], d[tv.z], d[tv.w] };
        float3 tp[4] = { P[tv.x], P[tv.y], P[tv.z], P[tv.w] };

        // Build 4-bit mask: bit set if value < IsoValue (inside)
        uint mask = (td[0] < IsoValue ? 1u : 0u)
                  | (td[1] < IsoValue ? 2u : 0u)
                  | (td[2] < IsoValue ? 4u : 0u)
                  | (td[3] < IsoValue ? 8u : 0u);

        if (mask == 0u || mask == 15u) continue;

        // Compute intersections for up to 6 edges in this tetra
        float3 vpos[6];
        [unroll] for (int e=0;e<6;e++){
            uint2 ab = kTetEdge[e];
            vpos[e] = interp(IsoValue, tp[ab.x], tp[ab.y], td[ab.x], td[ab.y]);
        }

        // Triangulate by small lookup table
        const int* tri = kMTri[mask];
        for (int i = 0; i < 6; i += 3){
            if (tri[i] < 0) break;

            float3 a = vpos[tri[i+0]];
            float3 b = vpos[tri[i+1]];
            float3 c = vpos[tri[i+2]];

            // Normal from density gradient (smooth) — robust for MT
            float3 nA = normalize(densityGrad(a));
            float3 nB = normalize(densityGrad(b));
            float3 nC = normalize(densityGrad(c));

            OutVerts.Append( (Vert) { a, nA } );
            OutVerts.Append( (Vert) { b, nB } );
            OutVerts.Append( (Vert) { c, nC } );
        }
    }
}
