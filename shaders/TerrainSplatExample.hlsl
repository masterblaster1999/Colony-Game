// -----------------------------------------------------------------------------
// shaders/TerrainSplatExample.hlsli
// Example VS/PS showing how to use terrain_splat_common.hlsl.
//
// NOTE:
// - This file is meant to be included from another .hlsl file.
// - Do NOT compile this file directly as a standalone shader.
// - Your "real" shader should #include this and choose appropriate
//   entry points (e.g. VSTerrain / PSTerrain) or wrap them in a main().
// -----------------------------------------------------------------------------

#ifndef TERRAIN_SPLAT_EXAMPLE_HLSLI
#define TERRAIN_SPLAT_EXAMPLE_HLSLI

#include "terrain_splat_common.hlsl"

// Matrices for transforming terrain vertices.
cbuffer TerrainMatrices : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
}

// Simple directional lighting + camera info.
cbuffer TerrainLighting : register(b1)
{
    float3 gLightDirW; float _pad0;
    float3 gLightColor; float _pad1;
    float3 gCameraPosW; float _pad2;
}

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};

struct VSOut
{
    float4 posH : SV_POSITION;
    float3 posW : TEXCOORD0;
    float3 nrmW : TEXCOORD1;
    float2 uv   : TEXCOORD2;
    float2 uvS  : TEXCOORD3;
};

// Example terrain vertex shader.
// Intended to be *used as* an entry point from another .hlsl file.
VSOut VSTerrain(VSIn IN)
{
    VSOut O;
    float4 pw = mul(float4(IN.pos, 1.0f), gWorld);
    O.posW = pw.xyz;

    // Assume uniform scale; otherwise use inverse-transpose for normals.
    O.nrmW = normalize(mul(float4(IN.nrm, 0.0f), gWorld).xyz);
    O.uv   = IN.uv;

    // Use mesh UV for splat by default (can also derive from world XZ).
    O.uvS  = IN.uv;

    float4 pv = mul(pw, gView);
    O.posH = mul(pv, gProj);
    return O;
}

struct PSOut
{
    float4 color : SV_TARGET;
};

// Example terrain pixel shader using terrain splat blending.
PSOut PSTerrain(VSOut IN)
{
    PSOut O;

    TerrainSplatIO io;
    io.posW    = IN.posW;
    io.nrmW    = normalize(IN.nrmW);
    io.uv      = IN.uv;
    io.uvSplat = IN.uvS;

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
    float  spec    = pow(ndoth, 32.0f); // simple Blinn-Phong
    float3 color   = diffuse * gLightColor + spec.xxx;

    // Small ambient term.
    color += 0.07f * albedo;

    O.color = float4(color, 1.0f);
    return O;
}

#endif // TERRAIN_SPLAT_EXAMPLE_HLSLI
