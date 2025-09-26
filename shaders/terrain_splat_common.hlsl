// -----------------------------------------------------------------------------
// shaders/terrain_splat_common.hlsl
// Helpers for terrain splat blending (4 materials via RGBA splat mask).
// Supports UV or Triplanar sampling (toggle via gUseTriplanar).
//
// Notes
//  - RGBA splat maps encode weights for up to 4 materials; weights are normalized
//    before blending. This is the standard "texture splatting" approach. 
//  - Triplanar sampling increases texture fetches (≈3×); use it when you need
//    to avoid UV stretching on cliffs. See Catlike Coding & Golus for details. 
// -----------------------------------------------------------------------------

#ifndef TERRAIN_SPLAT_COMMON_HLSL
#define TERRAIN_SPLAT_COMMON_HLSL

// --- Bindings -------------------------------------------------
Texture2D gMat0_Albedo : register(t0);
Texture2D gMat1_Albedo : register(t1);
Texture2D gMat2_Albedo : register(t2);
Texture2D gMat3_Albedo : register(t3);

Texture2D gSplatMask   : register(t4);

// Optional normals (tangent-space, BC5/RG or DXT5nm)
#ifdef TERRAIN_USE_NORMALS
Texture2D gMat0_Normal : register(t5);
Texture2D gMat1_Normal : register(t6);
Texture2D gMat2_Normal : register(t7);
Texture2D gMat3_Normal : register(t8);
#endif

SamplerState gSampAniso : register(s0);  // material sampling
SamplerState gSampMask  : register(s1);  // splat mask sampling

// --- Parameters -----------------------------------------------
cbuffer TerrainSplatParams : register(b2)
{
    // UV tiling for each material (albedo [+ normal])
    float gMat0_Tiling;
    float gMat1_Tiling;
    float gMat2_Tiling;
    float gMat3_Tiling;

    // Splat mask UV transform (maps world UV to splat texel space)
    float2 gSplatUVScale;  // multiply
    float2 gSplatUVOffset; // add

    // Triplanar controls
    int   gUseTriplanar;       // 0 = off (use mesh UV), 1 = on (use world-space triplanar)
    float gTriplanarSharpness; // how strongly to bias towards dominant axis (e.g., 4..16)
}

// Utility: renormalize RGBA weights to sum to 1 (avoid darkening)
float4 NormalizeWeights(float4 w)
{
    w = max(w, 0.0);
    float s = dot(w, 1.0.xxxx);
    return (s > 1e-5) ? w / s : float4(0,0,0,1); // fallback to A if all zero
}

// ------------------------ Triplanar sampling ------------------
// Blend weights per-axis from world-space normal.
float3 TriplanarAxisWeights(float3 nrmW, float sharpness)
{
    float3 w = abs(nrmW);
    // bias towards the dominant axis
    w = pow(w, sharpness.xxx);
    float sum = max(1e-5, w.x + w.y + w.z);
    return w / sum;
}

// Sample a Texture2D using world-space triplanar projection.
float3 TriplanarSampleRGB(Texture2D tex, float3 posW, float3 nrmW, float tile)
{
    float3 w = TriplanarAxisWeights(nrmW, gTriplanarSharpness);

    // Project onto the 3 cardinal planes and sample
    float2 uvX = posW.zy * tile;            // projection on YZ plane (X axis)
    float2 uvY = posW.xz * tile;            // projection on XZ plane (Y axis)
    float2 uvZ = posW.xy * tile;            // projection on XY plane (Z axis)

    float3 xSample = tex.Sample(gSampAniso, uvX).rgb;
    float3 ySample = tex.Sample(gSampAniso, uvY).rgb;
    float3 zSample = tex.Sample(gSampAniso, uvZ).rgb;

    return xSample * w.x + ySample * w.y + zSample * w.z;
}

// (Optional) very lightweight normal decode from BC5/DXT5nm style data.
// Assumes XY stored in RG in [-1,1], reconstruct Z.
float3 DecodeNormalRG(float2 rg)
{
    float3 n = float3(rg * 2.0 - 1.0, 0.0);
    n.z = sqrt(saturate(1.0 - dot(n.xy, n.xy)));
    return normalize(n);
}

#ifdef TERRAIN_USE_NORMALS
float3 TriplanarSampleNormal(Texture2D tex, float3 posW, float3 nrmW, float tile)
{
    float3 w = TriplanarAxisWeights(nrmW, gTriplanarSharpness);

    float2 uvX = posW.zy * tile;
    float2 uvY = posW.xz * tile;
    float2 uvZ = posW.xy * tile;

    // Approximate: sample each projection's tangent-space normal and treat XY as world XY of that plane.
    // For production, see Golus for correct reorientation into world space.
    float3 nx = DecodeNormalRG(tex.Sample(gSampAniso, uvX).rg);
    float3 ny = DecodeNormalRG(tex.Sample(gSampAniso, uvY).rg);
    float3 nz = DecodeNormalRG(tex.Sample(gSampAniso, uvZ).rg);

    // Remap each to world-ish contribution along the respective axis basis.
    // This is a simplified approach to keep the footprint tiny.
    float3 n =
        float3( nx.z, nx.x, nx.y) * w.x +   // project X-plane normal
        float3( ny.x, ny.z, ny.y) * w.y +   // project Y-plane normal
        float3( nz.x, nz.y, nz.z) * w.z;    // project Z-plane normal

    return normalize(n);
}
#endif

// ------------------------ Splat blending ----------------------
struct TerrainSplatIO
{
    float3 posW;     // world position
    float3 nrmW;     // world normal (for triplanar)
    float2 uv;       // mesh UV (for non-triplanar)
    float2 uvSplat;  // UV for splat mask
};

// Sample albedo for one material by UV or triplanar.
float3 SampleMatAlbedo(int matIdx, TerrainSplatIO I)
{
    if (gUseTriplanar != 0)
    {
        if      (matIdx == 0) return TriplanarSampleRGB(gMat0_Albedo, I.posW, I.nrmW, gMat0_Tiling);
        else if (matIdx == 1) return TriplanarSampleRGB(gMat1_Albedo, I.posW, I.nrmW, gMat1_Tiling);
        else if (matIdx == 2) return TriplanarSampleRGB(gMat2_Albedo, I.posW, I.nrmW, gMat2_Tiling);
        else                  return TriplanarSampleRGB(gMat3_Albedo, I.posW, I.nrmW, gMat3_Tiling);
    }
    else
    {
        if      (matIdx == 0) return gMat0_Albedo.Sample(gSampAniso, I.uv * gMat0_Tiling).rgb;
        else if (matIdx == 1) return gMat1_Albedo.Sample(gSampAniso, I.uv * gMat1_Tiling).rgb;
        else if (matIdx == 2) return gMat2_Albedo.Sample(gSampAniso, I.uv * gMat2_Tiling).rgb;
        else                  return gMat3_Albedo.Sample(gSampAniso, I.uv * gMat3_Tiling).rgb;
    }
}

#ifdef TERRAIN_USE_NORMALS
float3 SampleMatNormal(int matIdx, TerrainSplatIO I)
{
    if (gUseTriplanar != 0)
    {
        if      (matIdx == 0) return TriplanarSampleNormal(gMat0_Normal, I.posW, I.nrmW, gMat0_Tiling);
        else if (matIdx == 1) return TriplanarSampleNormal(gMat1_Normal, I.posW, I.nrmW, gMat1_Tiling);
        else if (matIdx == 2) return TriplanarSampleNormal(gMat2_Normal, I.posW, I.nrmW, gMat2_Tiling);
        else                  return TriplanarSampleNormal(gMat3_Normal, I.posW, I.nrmW, gMat3_Tiling);
    }
    else
    {
        float2 uv = I.uv;
        float3 n;
        if      (matIdx == 0) n = DecodeNormalRG(gMat0_Normal.Sample(gSampAniso, uv * gMat0_Tiling).rg);
        else if (matIdx == 1) n = DecodeNormalRG(gMat1_Normal.Sample(gSampAniso, uv * gMat1_Tiling).rg);
        else if (matIdx == 2) n = DecodeNormalRG(gMat2_Normal.Sample(gSampAniso, uv * gMat2_Tiling).rg);
        else                  n = DecodeNormalRG(gMat3_Normal.Sample(gSampAniso, uv * gMat3_Tiling).rg);
        return n;
    }
}
#endif

// Main entry: returns blended albedo (and optionally normal) given IO.
float3 TerrainBlend_Albedo(TerrainSplatIO I)
{
    float2 uvS = I.uvSplat * gSplatUVScale + gSplatUVOffset;
    float4 w   = gSplatMask.Sample(gSampMask, uvS); // RGBA weights
    w = NormalizeWeights(w);

    float3 a0 = SampleMatAlbedo(0, I);
    float3 a1 = SampleMatAlbedo(1, I);
    float3 a2 = SampleMatAlbedo(2, I);
    float3 a3 = SampleMatAlbedo(3, I);
    return a0*w.r + a1*w.g + a2*w.b + a3*w.a;
}

#ifdef TERRAIN_USE_NORMALS
float3 TerrainBlend_Normal(TerrainSplatIO I)
{
    float2 uvS = I.uvSplat * gSplatUVScale + gSplatUVOffset;
    float4 w   = gSplatMask.Sample(gSampMask, uvS);
    w = NormalizeWeights(w);

    float3 n0 = SampleMatNormal(0, I);
    float3 n1 = SampleMatNormal(1, I);
    float3 n2 = SampleMatNormal(2, I);
    float3 n3 = SampleMatNormal(3, I);
    // Weighted normalized blend (simple; for more accurate blending use RNM)
    return normalize(n0*w.r + n1*w.g + n2*w.b + n3*w.a);
}
#endif

#endif // TERRAIN_SPLAT_COMMON_HLSL
