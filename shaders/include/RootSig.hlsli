// ============================================================================
// RootSig.hlsli  --  Common root signatures for Direct3D 12 HLSL (Windows)
// Install path:  shaders/include/RootSig.hlsli
//
// Usage (in a shader):
//   #include "RootSig.hlsli"
//   [RootSignature(RS_GRAPHICS_STATIC_SAMPLERS)]
//   float4 mainPS(...) : SV_Target { ... }
//
// Or bindless-style (Shader Model 6.6+):
//   [RootSignature(RS_GRAPHICS_BINDLESS)]
//   // Access ResourceDescriptorHeap[] / SamplerDescriptorHeap[] in HLSL.
// ============================================================================

#ifndef ROOTSIG_HLSLI_INCLUDED
#define ROOTSIG_HLSLI_INCLUDED

// ------------------------------ Notes ---------------------------------------
// - RS_*_STATIC_SAMPLERS: Uses descriptor tables for SRV/UAV/CBV and 3 static
//   samplers (s0..s2).
// - RS_*_SAMPLER_TABLE:    Uses a Sampler descriptor table (s0..), not static
//   samplers (helps when you vary sampler state at runtime).
// - RS_*_BINDLESS:         Shader Model 6.6 "directly indexed" descriptor heaps.
//   Requires compiling shaders for SM 6.6+ and enabling these flags in RootFlags.
//   (You still bind your global descriptor heaps from C++; the shader indexes them.)
//
// All variants place small per-draw/dispatch constants in the root via
// RootConstants(b0). Descriptor-table CBVs start at b1 to avoid overlap.
//
// Visibility is left at SHADER_VISIBILITY_ALL for simplicity. Specialize to
// VS/PS/CS to reduce broadcast cost when needed.
// ----------------------------------------------------------------------------

// ---------------------- Graphics (VS/PS/GS/HS/DS) ----------------------------

// 1) Graphics + Static Samplers (general purpose)
#define RS_GRAPHICS_STATIC_SAMPLERS \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
  "DescriptorTable(SRV(t0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,  numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  /* s0: linear wrap, s1: point clamp, s2: anisotropic wrap */ \
  "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR,  addressU=TEXTURE_ADDRESS_WRAP,  addressV=TEXTURE_ADDRESS_WRAP,  addressW=TEXTURE_ADDRESS_WRAP)," \
  "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT,   addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP)," \
  "StaticSampler(s2, filter=FILTER_ANISOTROPIC,         addressU=TEXTURE_ADDRESS_WRAP,  addressV=TEXTURE_ADDRESS_WRAP,  addressW=TEXTURE_ADDRESS_WRAP, maxAnisotropy=8)," \
  "RootConstants(num32BitConstants=16, b0)"

// 2) Graphics + Sampler Descriptor Table (no static samplers)
#define RS_GRAPHICS_SAMPLER_TABLE \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
  "DescriptorTable(SRV(t0,     numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,     numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,     numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(Sampler(s0, numDescriptors=16),        visibility=SHADER_VISIBILITY_ALL)," \
  "RootConstants(num32BitConstants=16, b0)"

// 3) Graphics + SM 6.6 Dynamic Resources (directly indexed heaps)
#define RS_GRAPHICS_BINDLESS \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | " \
             "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  "RootConstants(num32BitConstants=16, b0)"

// ------------------------------ Compute --------------------------------------

// 4) Compute + Static Samplers
#define RS_COMPUTE_STATIC_SAMPLERS \
  "RootFlags(0)," \
  "DescriptorTable(SRV(t0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(UAV(u0,  numDescriptors=unbounded), visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b1,  numDescriptors=32),        visibility=SHADER_VISIBILITY_ALL)," \
  "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR,  addressU=TEXTURE_ADDRESS_WRAP,  addressV=TEXTURE_ADDRESS_WRAP,  addressW=TEXTURE_ADDRESS_WRAP)," \
  "StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT,   addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP)," \
  "RootConstants(num32BitConstants=16, b0)"

// 5) Compute + SM 6.6 Dynamic Resources (directly indexed heaps)
#define RS_COMPUTE_BINDLESS \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  "RootConstants(num32BitConstants=16, b0)"

// ----------------------------- Tips ------------------------------------------
// * If you deny access to unused shader stages, append e.g.:
//   DENY_GEOMETRY_SHADER_ROOT_ACCESS, etc., to RootFlags(...).
// * If you need unbounded sampler tables, change numDescriptors=unbounded in
//   the Sampler() range (requires Tier 2/3 binding and RS 1.1-level parsing).
// * Keep registers consistent with your engine-side binding model.
// -----------------------------------------------------------------------------

#endif // ROOTSIG_HLSLI_INCLUDED
