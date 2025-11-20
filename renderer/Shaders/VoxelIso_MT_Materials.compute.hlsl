// renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl
// Marching Tetrahedra with per-triangle 'materialId' classification for cave splat/decals.
// Output: AppendStructuredBuffer of vertices incl. uint materialId.
// SM 5.0+ required.
//
// Background: Marching Tetrahedra avoids classic MC ambiguities and uses a small case table.
// See also: regularized MT and general MT references.
//
// ---------------------------------------------------------------

#ifndef MT_THREADS_X
#define MT_THREADS_X 4
#endif
#ifndef MT_THREADS_Y
#define MT_THREADS_Y 4
#endif
#ifndef MT_THREADS_Z
#define MT_THREADS_Z 4
#endif

struct VertMat {
    float3 pos;     // world-space
    float3 nrm;     // world-space, unit
    uint   matId;   // material classification id
};

cbuffer VoxelCB : register(b0)
{
    uint3 VolumeSize;      // number of voxel corners along axes (cubes = VolumeSize-1)
    float  CellSize;       // world meters per cell
    float3 OriginWS;       // world origin of (0,0,0)
    float  IsoValue;       // 0 by default

    uint   Seed;           // noise seed
    float  NoiseFreq;      // base freq for 3D value noise (cycles/m)
    float  NoiseGain;      // fBm gain (~0.5)
    float  NoiseLacun;     // fBm lacunarity (~2.0)
    int    NoiseOctaves;   // fBm octaves (3..6)
    float  WarpStrength;   // domain-warp strength (meters)
    float  GradDelta;      // gradient epsilon for normals (meters)
};

cbuffer CaveMatCB : register(b1)
{
    float3 UpWS;        float _pad0;  // usually (0,1,0)
    float  WaterLevel;  float OreDepthMin; // world-space thresholds

    // Noise frequencies for classification
    float  WetnessFreq; float MossFreq; float OreFreq; float _pad1;

    // Classification thresholds (0..1)
    float  WetnessThreshold;
    float  MossThreshold;
    float  OreThreshold;
    float  _pad2;
};

AppendStructuredBuffer<VertMat> OutVerts : register(u0);

// --------------------- Utility Noise (3D value + fBm + warp) ---------------------
float hash31(float3 p) { return frac(sin(dot(p, float3(127.1,311.7,74.7))) * 43758.5453123); }

float valueNoise3D(float3 p)
{
    float3 i = floor(p), f = frac(p);
    float n000 = hash31(i + float3(0,0,0));
    float n100 = hash31(i + float3(1,0,0));
    float n010 = hash31(i + float3(0,1,0));
    float n110 = hash31(i + float3(1,1,0));
    float n001 = hash31(i + float3(0,0,1));
    float n101 = hash31(i + float3(1,0,1));
    float n011 = hash31(i + float3(0,1,1));
    float n111 = hash31(i + float3(1,1,1));
    float3 u = f*f*(3.0-f*2.0);
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

float fbm3D(float3 p, float freq, float lacun, float gain, int oct)
{
    float v=0.0, amp=0.5, f=freq;
    [unroll] for(int i=0;i<12;i++){
        if(i>=oct) break;
        v += amp * valueNoise3D(p * f);
        f *= lacun;
        amp *= gain;
    }
    return v;
}

float3 warp3(float3 p, float scale, float strength)
{
    float3 w = float3(
        valueNoise3D(p*scale + 11.0),
        valueNoise3D(p*scale + 23.0),
        valueNoise3D(p*scale + 47.0)
    );
    return p + (w*2.0-1.0) * strength;
}

// --------------------- Density field (implicit cave) ---------------------
float densityField(float3 pWS)
{
    float3 pw = pWS * (NoiseFreq * 0.35) + Seed * 0.137;
    float3 q  = warp3(pw, 1.0, WarpStrength);
    float val = fbm3D(q, NoiseFreq, NoiseLacun, NoiseGain, NoiseOctaves);
    return val - 0.5; // center around 0
}

float3 densityGrad(float3 p)
{
    float h = GradDelta;
    float dx = densityField(p+float3(h,0,0)) - densityField(p-float3(h,0,0));
    float dy = densityField(p+float3(0,h,0)) - densityField(p-float3(0,h,0));
    float dz = densityField(p+float3(0,0,h)) - densityField(p-float3(0,0,h));
    return float3(dx,dy,dz)/(2.0*h);
}

// --------------------- Material classification ---------------------
uint classifyMaterial(float3 posWS, float3 nrmWS)
{
    // Up-facing (> 0.6), down-facing (< -0.6), walls otherwise
    float updot = dot(normalize(nrmWS), normalize(UpWS));
    float depthBelowWater = WaterLevel - posWS.y;

    // Local noises for variety (world-space stable)
    float wet  = fbm3D(posWS + Seed * 0.33, WetnessFreq, 2.0, 0.5, 4);
    float moss = fbm3D(posWS + Seed * 0.67, MossFreq,   2.0, 0.5, 4);
    float ore  = fbm3D(posWS + Seed * 0.99, OreFreq,    2.0, 0.5, 3);

    const uint MAT_LIMESTONE = 0;
    const uint MAT_DRIPSTONE = 1;
    const uint MAT_MUD       = 2;
    const uint MAT_MOSS      = 3;
    const uint MAT_ORE       = 4;

    // Ceilings → DRIPSTONE
    if (updot < -0.6) {
        return MAT_DRIPSTONE;
    }

    // Floors → MUD if wet enough
    if (updot > 0.6) {
        return (wet > WetnessThreshold) ? MAT_MUD : MAT_LIMESTONE;
    }

    // Walls: prefer ORE veins if deep & ore noise hits; else MOSS if damp; else LIMESTONE
    bool deepEnough = (posWS.y < OreDepthMin);
    if (deepEnough && ore > OreThreshold) return MAT_ORE;
    if (wet > WetnessThreshold && moss > MossThreshold) return MAT_MOSS;
    return MAT_LIMESTONE;
}

// --------------------- MT tables ---------------------
static const uint4 kTets[6] = {
    uint4(0,1,2,7), uint4(0,1,5,7), uint4(0,2,3,7),
    uint4(0,2,6,7), uint4(0,4,5,7), uint4(0,4,6,7)
};

static const uint2 kTetEdge[6] = {
    uint2(0,1), uint2(1,2), uint2(2,0),
    uint2(0,3), uint2(1,3), uint2(2,3)
};

// 16 cases → up to 2 triangles (edge ids), -1 terminator
static const int kMTri[16][7] = {
    {-1,-1,-1,-1,-1,-1,-1},
    { 0,3,2, -1,-1,-1,-1},
    { 0,1,4, -1,-1,-1,-1},
    { 1,4,2,  2,4,3, -1},
    { 1,2,5, -1,-1,-1,-1},
    { 0,3,5,  0,5,1, -1},
    { 0,2,5,  0,5,4, -1},
    { 3,5,4, -1,-1,-1,-1},
    { 3,4,5, -1,-1,-1,-1},
    { 0,4,5,  0,5,2, -1},
    { 0,5,1,  0,3,5, -1},
    { 1,5,2, -1,-1,-1,-1},
    { 1,3,4,  1,2,3, -1},
    { 0,4,1, -1,-1,-1,-1},
    { 0,2,3, -1,-1,-1,-1},
    {-1,-1,-1,-1,-1,-1,-1}
};

float3 interp(float iso, float3 p0, float3 p1, float d0, float d1) {
    float t = (iso - d0) / (d1 - d0 + 1e-8);
    return lerp(p0, p1, saturate(t));
}

[numthreads(MT_THREADS_X, MT_THREADS_Y, MT_THREADS_Z)]
void CSMain(uint3 gtid : SV_DispatchThreadID)
{
    if (gtid.x >= VolumeSize.x-1 || gtid.y >= VolumeSize.y-1 || gtid.z >= VolumeSize.z-1) return;

    // Cube corner world positions
    float3 base = OriginWS + float3(gtid) * CellSize;
    float3 P[8] = {
        base + float3(0,0,0),               base + float3(CellSize,0,0),
        base + float3(0,CellSize,0),        base + float3(CellSize,CellSize,0),
        base + float3(0,0,CellSize),        base + float3(CellSize,0,CellSize),
        base + float3(0,CellSize,CellSize), base + float3(CellSize,CellSize,CellSize)
    };

    // Densities
    float d[8];
    [unroll] for (int i=0;i<8;i++) d[i] = densityField(P[i]);

    // March the 6 tetrahedra
    [unroll] for (int t=0;t<6;t++)
    {
        uint4 tv    = kTets[t];
        float td[4] = { d[tv.x], d[tv.y], d[tv.z], d[tv.w] };
        float3 tp[4]= { P[tv.x], P[tv.y], P[tv.z], P[tv.w] };

        uint mask = (td[0] < IsoValue ? 1u:0u) | (td[1] < IsoValue ? 2u:0u)
                  | (td[2] < IsoValue ? 4u:0u) | (td[3] < IsoValue ? 8u:0u);
        if (mask == 0u || mask == 15u) continue;

        // Edge intersections
        float3 vpos[6];
        [unroll] for (int e=0;e<6;e++){
            uint2 ab = kTetEdge[e];
            vpos[e] = interp(IsoValue, tp[ab.x], tp[ab.y], td[ab.x], td[ab.y]);
        }

        // Load triangle pattern row for this case into a local array.
        // This replaces the illegal "const int* tri = kMTri[mask];" pointer usage.
        int tri[7];
        [unroll]
        for (int idx = 0; idx < 7; ++idx)
        {
            tri[idx] = kMTri[mask][idx];
        }

        [unroll]
        for (int i=0;i<6;i+=3)
        {
            if (tri[i] < 0) break;

            float3 a = vpos[tri[i+0]];
            float3 b = vpos[tri[i+1]];
            float3 c = vpos[tri[i+2]];

            // Smooth normals from field gradient
            float3 nA = normalize(densityGrad(a));
            float3 nB = normalize(densityGrad(b));
            float3 nC = normalize(densityGrad(c));

            // One materialId for the triangle (centroid + averaged normal)
            float3 center = (a+b+c)/3.0;
            float3 nAvg   = normalize(nA+nB+nC);
            uint   matId  = classifyMaterial(center, nAvg);

            // HLSL does not support C99-style compound literals, so we build VertMat
            // instances explicitly and append them.
            VertMat vA;
            vA.pos   = a;
            vA.nrm   = nA;
            vA.matId = matId;
            OutVerts.Append(vA);

            VertMat vB;
            vB.pos   = b;
            vB.nrm   = nB;
            vB.matId = matId;
            OutVerts.Append(vB);

            VertMat vC;
            vC.pos   = c;
            vC.nrm   = nC;
            vC.matId = matId;
            OutVerts.Append(vC);
        }
    }
}
