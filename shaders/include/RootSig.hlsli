// ============================================================================
// RootSig.hlsli  --  Common root signatures for Direct3D 12 HLSL (Windows)
// Install path:  shaders/include/RootSig.hlsli
//
// Usage (in a shader):
//   #include "RootSig.hlsli"            // or a relative path from the shader
//
//   [RootSignature(RS_GRAPHICS_STATIC_SAMPLERS)]
//   float4 mainPS(...) : SV_Target { ... }
//
// Or bindless-style (Shader Model 6.6+):
//   [RootSignature(RS_GRAPHICS_BINDLESS)]
//   // Access ResourceDescriptorHeap[] / SamplerDescriptorHeap[] in HLSL.
// ============================================================================

#ifndef ROOTSIG_HLSLI_INCLUDED
#define ROOTSIG_HLSLI_INCLUDED

// ------------------------------ Overview ------------------------------------
// Layout conventions used in all root signatures:
//
//   * RootConstants(b0)       : per-draw/dispatch constants (up to 16 x uint).
//   * CBV descriptor table b1 : "global" constant buffers.
//   * SRV descriptor table t0 : textures / structured buffers.
//   * UAV descriptor table u0 : UAV buffers / RWTextures.
//   * Samplers                : either static samplers (s0..) or a table (s0..).
//
// Visibility is SHADER_VISIBILITY_ALL for simplicity. Clone & specialize
// these macros if you want tighter, stage-specific root signatures.
// ----------------------------------------------------------------------------

// Helper snippet reused by graphics + compute variants.
#define RS_STATIC_SAMPLERS_DEFAULT \
  /* s0: linear wrap */ \
  "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR,  " \
  "              addressU=TEXTURE_ADDRESS_WRAP,  addressV=TEXTURE_ADDRESS_WRAP,  addressW=TEXTURE_ADDRESS_WRAP)," \
  /* s1: point clamp (integer / shadow maps, etc.) */ \
  "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT,   " \
  "              addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP)," \
  /* s2: anisotropic wrap */ \
  "StaticSampler(s2, filter=FILTER_ANISOTROPIC,         " \
  "              addressU=TEXTURE_ADDRESS_WRAP,  addressV=TEXTURE_ADDRESS_WRAP,  addressW=TEXTURE_ADDRESS_WRAP, maxAnisotropy=8),"

// Common 16 x 32‑bit root constant block at b0.
#define RS_ROOT_CONSTANTS_16 \
  "RootConstants(num32BitConstants=16, b0)"

// ---------------------- Graphics (VS/PS/GS/HS/DS) ----------------------------
//
// These are meant for a "classic" VS/PS pipeline. If you *know* you don't use
// HS/DS/GS, copy one of these macros and add:
//
//   DENY_HULL_SHADER_ROOT_ACCESS |
//   DENY_DOMAIN_SHADER_ROOT_ACCESS |
//   DENY_GEOMETRY_SHADER_ROOT_ACCESS
//
// to the RootFlags(...) list for slightly cheaper root signatures.
// ----------------------------------------------------------------------------

// 1) Graphics + Static Samplers (general purpose)
#define RS_GRAPHICS_STATIC_SAMPLERS \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
  "DescriptorTable(SRV(t0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,  numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  RS_STATIC_SAMPLERS_DEFAULT \
  RS_ROOT_CONSTANTS_16

// 2) Graphics + Sampler Descriptor Table (no static samplers)
#define RS_GRAPHICS_SAMPLER_TABLE \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
  "DescriptorTable(SRV(t0,     numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,     numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,     numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(Sampler(s0, numDescriptors=16),        visibility=SHADER_VISIBILITY_ALL)," \
  RS_ROOT_CONSTANTS_16

// 3) Graphics + SM 6.6 Dynamic Resources (directly indexed heaps)
//
// Requires:
//   * Shader Model 6.6+ (e.g. vs_6_6 / ps_6_6).
//   * Global descriptor heaps bound from C++ via SetDescriptorHeaps.
//   * RootFlags(...HEAP_DIRECTLY_INDEXED) enabled below.
#define RS_GRAPHICS_BINDLESS \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | " \
             "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  RS_ROOT_CONSTANTS_16

// ------------------------------ Compute --------------------------------------

// 4) Compute + Static Samplers
#define RS_COMPUTE_STATIC_SAMPLERS \
  "RootFlags(0)," \
  "DescriptorTable(SRV(t0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,  numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  RS_STATIC_SAMPLERS_DEFAULT \
  RS_ROOT_CONSTANTS_16

// 5) Compute + SM 6.6 Dynamic Resources (directly indexed heaps)
#define RS_COMPUTE_BINDLESS \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  RS_ROOT_CONSTANTS_16

// ----------------------------- Tips / Notes ----------------------------------
//
// * Typical usage:
//
//       [RootSignature(RS_GRAPHICS_STATIC_SAMPLERS)]
//       float4 mainPS(...) : SV_Target { ... }
//
//   or compiled as a standalone root signature with a rootsig_1_0 / rootsig_1_1
//   profile.
//
// * If you need different layouts per shader stage, copy one of the macros
//   above and specialize:
//     - RootFlags(...) with DENY_*_ROOT_ACCESS bits.
//     - DescriptorTable ranges (e.g. fixed numDescriptors instead of unbounded).
//
// * Keep the register usage (b0/b1, t0, u0, s0..) in sync with your C++ binding
//   code when you build the ID3D12RootSignature.
//
// ----------------------------------------------------------------------------- 

#endif // ROOTSIG_HLSLI_INCLUDED
