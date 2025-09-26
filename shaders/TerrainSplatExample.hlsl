// -----------------------------------------------------------------------------
// shaders/TerrainSplatExample.hlsl
// Minimal VS/PS showing how to use terrain_splat_common.hlsl
// -----------------------------------------------------------------------------

#include "terrain_splat_common.hlsl"

cbuffer TerrainMatrices : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
}

cbuffer TerrainLighting : register(b1)
{
    float3 gLightDirW; float _pad0;
    float3 gLightColor; float _pad1;
    float3 gCameraPosW; float _pad2;
}

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};

struct VSOut {
    float4 posH : SV_POSITION;
    float3 posW : TEXCOORD0;
    float3 nrmW : TEXCOORD1;
    float2 uv   : TEXCOORD2;
    float2 uvS  : TEXCOORD3;
};

VSOut VSTerrain(VSIn IN)
{
    VSOut O;
    float4 pw = mul(float4(IN.pos,1), gWorld);
    O.posW = pw.xyz;

    // Assume uniform scale; otherwise use inverse-transpose for normals
    O.nrmW = normalize(mul(float4(IN.nrm,0), gWorld).xyz);
    O.uv   = IN.uv;

    // Use mesh UV for splat by default (can also derive from world XZ)
    O.uvS  = IN.uv;

    float4 pv = mul(pw, gView);
    O.posH = mul(pv, gProj);
    return O;
}

struct PSOut { float4 color : SV_TARGET; };

PSOut PSTerrain(VSOut IN)
{
    PSOut O;

    TerrainSplatIO io;
    io.posW   = IN.posW;
    io.nrmW   = normalize(IN.nrmW);
    io.uv     = IN.uv;
    io.uvSplat= IN.uvS;

    float3 albedo = TerrainBlend_Albedo(io);

#ifdef TERRAIN_USE_NORMALS
    float3 N = TerrainBlend_Normal(io);
#else
    float3 N = normalize(IN.nrmW);
#endif

    float3 L = normalize(gLightDirW);
    float3 V = normalize(gCameraPosW - IN.posW);
    float3 H = normalize(L + V);

    float ndotl = saturate(dot(N, L));
    float ndoth = saturate(dot(N, H));

    float3 diffuse = albedo * ndotl;
    float  spec    = pow(ndoth, 32.0); // simple Blinn-Phong
    float3 color   = diffuse * gLightColor + spec.xxx;

    // small ambient term
    color += 0.07 * albedo;

    O.color = float4(color, 1.0);
    return O;
}
