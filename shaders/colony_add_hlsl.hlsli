// -------------------------------------------------------------------------------------------------
// colony_add_hlsl.hlsli
// Small, safe HLSL helper header for Colony-Game (Windows-only).
// - Compatible with FXC (SM5.x, D3D11) and DXC (SM6.x, D3D12).
// - Purely optional utilities: no cbuffers or hard-coded resource bindings here.
// - Keep this file lightweight: helpers that many shaders use repeatedly.
//
// Usage:
//   #include "colony_add_hlsl.hlsli"
//
// License: MIT (match the project)
//
// -------------------------------------------------------------------------------------------------

#ifndef COLONY_ADD_HLSL_INCLUDED
#define COLONY_ADD_HLSL_INCLUDED

// ----- Compiler/feature probes -------------------------------------------------
#if !defined(CG_SM6)
  // DXC typically defines __DXC__; if you prefer, define CG_SM6 from your build system for SM6.
  #if defined(__DXC__)
    #define CG_SM6 1
  #else
    #define CG_SM6 0
  #endif
#endif

// DXC supports #pragma once; FXC tends to ignore it but it’s harmless.
#pragma once

// ----- Common constants --------------------------------------------------------
#ifndef CG_PI
  #define CG_PI 3.14159265358979323846
#endif
#ifndef CG_TAU
  #define CG_TAU 6.28318530717958647692
#endif
#ifndef CG_EPS
  #define CG_EPS 1e-6
#endif

// ----- Attribute & flow-hints (portable) --------------------------------------
#ifndef CG_INLINE
  #define CG_INLINE inline
#endif

#ifndef CG_UNROLL
  #define CG_UNROLL(N) [unroll(N)]
#endif

#ifndef CG_BRANCH
  #define CG_BRANCH [branch]
#endif

#ifndef CG_FLATTEN
  #define CG_FLATTEN [flatten]
#endif

// ----- Root-signature helper (no-op on D3D11/FXC) -----------------------------
// Use: RS_DEF("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) ...")
#if CG_SM6
  #define RS_DEF(STR) [RootSignature(STR)]
#else
  #define RS_DEF(STR)
#endif

// ----- Resource declaration helpers (optional) --------------------------------
// Keep declarations terse without imposing binding indices.
#define CG_TEX2D(name)           Texture2D name
#define CG_TEX2DMS(name, SAMPLES) Texture2DMS<float4, SAMPLES> name
#define CG_SAMPLER(name)         SamplerState name
#define CG_SAMPLER_CMP(name)     SamplerComparisonState name

// cbuffer helper with explicit register only if caller provides it:
//   CG_CBUFFER(Frame, b0) { float2 ScreenSize; float Time; }
#define _CG_PRIMITIVE_CAT(a,b) a##b
#define _CG_PRIMITIVE_CAT3(a,b,c) a##b##c
#define CG_CBUFFER(name, ...) cbuffer name : register(__VA_ARGS__)

// ----- Math helpers ------------------------------------------------------------
CG_INLINE float  saturate01(float x)        { return saturate(x); }
CG_INLINE float2 saturate01(float2 x)       { return saturate(x); }
CG_INLINE float3 saturate01(float3 x)       { return saturate(x); }
CG_INLINE float4 saturate01(float4 x)       { return saturate(x); }

CG_INLINE float  remap(float v, float a, float b, float c, float d)
{
    return (v - a) * (d - c) / max(b - a, CG_EPS) + c;
}
CG_INLINE float2 remap(float2 v, float2 a, float2 b, float2 c, float2 d)
{
    return (v - a) * (d - c) / max(b - a, CG_EPS) + c;
}

CG_INLINE float3 safeNormalize(float3 v)
{
    float len2 = dot(v, v);
    return (len2 > CG_EPS) ? v * rsqrt(len2) : float3(0,0,0);
}

CG_INLINE float2x2 rot2x2(float radians)
{
    float s = sin(radians), c = cos(radians);
    return float2x2(c, -s,
                    s,  c);
}

// Hashes (low-discrepancy noise helpers)
CG_INLINE float hash11(float p)
{
    // Dave Hoskins-like hash
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}
CG_INLINE float hash21(float2 p)
{
    // 2->1 hash
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac(p.x * p.y);
}
CG_INLINE float3 hash33(float3 p)
{
    // 3->3 hash
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 33.33);
    return frac((p.xxy + p.yxx) * (p.zyx + p.yzz));
}

// ----- Color helpers -----------------------------------------------------------
// sRGB <-> linear conversions (approximate, good enough for UI/post)
CG_INLINE float3 srgbToLinear(float3 c)
{
    return pow(c, 2.2);
}
CG_INLINE float3 linearToSrgb(float3 c)
{
    return pow(saturate(c), 1.0 / 2.2);
}

// Narkowicz ACES-ish tonemap (fast)
CG_INLINE float3 tonemapACES(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x*(a*x + b)) / (x*(c*x + d) + e));
}

// ----- Fullscreen triangle VS helper ------------------------------------------
// Use this for simple post-process passes; bind a dummy IA (or none with
// ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) and rely on SV_VertexID.
struct CG_FullscreenVSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

CG_INLINE CG_FullscreenVSOut CG_FullscreenVS(uint vid)
{
    // 3-vertex fullscreen triangle (no VB/IB)
    //   vid: 0 -> (-1,-1), 1 -> (-1,3), 2 -> (3,-1)
    float2 p   = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    CG_FullscreenVSOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv  = float2(0.5 * (p + 1.0));
    // If your target is not texture-space Y-up, flip here as needed:
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

// ----- Sampling helpers --------------------------------------------------------
CG_INLINE float4 SampleLinear(Texture2D tex, SamplerState s, float2 uv)
{
    return tex.Sample(s, uv);
}
CG_INLINE float3 SampleLinearSRGB(Texture2D tex, SamplerState s, float2 uv)
{
    float3 c = tex.Sample(s, uv).rgb;
    return srgbToLinear(c);
}

// Convenience for tapping a mip level without derivatives (be careful)
CG_INLINE float4 SampleLevel(Texture2D tex, SamplerState s, float2 uv, float level)
{
    return tex.SampleLevel(s, uv, level);
}

#endif // COLONY_ADD_HLSL_INCLUDED
