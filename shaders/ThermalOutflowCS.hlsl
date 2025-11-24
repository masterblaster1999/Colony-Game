// ThermalOutflowCS.hlsl
// Pass 1: Given input height map, compute per-pixel outflow to 4 neighbors.
// Stores outflow as float4: (+X, -X, +Y, -Y)
//
// Expected compile settings (DXC / Visual Studio HLSL):
//   Shader Type   : Compute
//   Shader Model  : cs_6_0   (MUST be 6.0+ with DXC)
//   Entry Point   : CSMain   (or 'main' if you prefer the alias below)
//
// CBuffer layout must match the CPU side (Width/Height/Talus/Strength).

//-----------------------------------------------------------------------------
// Shader Model guard: fail early if someone tries to compile for < SM 6.0
// This turns "Promoting older shader model profile to 6.0" into a clear error
// instead of a noisy DXC warning.
//-----------------------------------------------------------------------------
#if defined(__SHADER_TARGET_MAJOR) && (__SHADER_TARGET_MAJOR < 6)
    #error "ThermalOutflowCS.hlsl must be compiled for Shader Model 6.0 or higher (e.g. /T cs_6_0)."
#endif

//-----------------------------------------------------------------------------
// Configurable thread-group size (engine can override via /D if desired)
//-----------------------------------------------------------------------------
#ifndef THERMAL_OUTFLOW_THREAD_GROUP_SIZE_X
    #define THERMAL_OUTFLOW_THREAD_GROUP_SIZE_X 16
#endif

#ifndef THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Y
    #define THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Y 16
#endif

#ifndef THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Z
    #define THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Z 1
#endif

// Small numeric epsilon used to guard against degenerate cases if needed.
static const float kEpsilon = 1e-6f;

cbuffer ErodeCB : register(b0)
{
    uint  Width;
    uint  Height;
    float Talus;     // minimum slope before material moves (0..1 recommended)
    float Strength;  // max fraction of (sum slopes - talus) to move (0..1 recommended)
};

// Input height field
Texture2D<float>    gInHeight : register(t0);

// Output outflow to 4 cardinal neighbors: (+X, -X, +Y, -Y)
RWTexture2D<float4> gOutflow  : register(u0);

// Optional helper: load height with uint2 coords (slightly nicer callsite)
inline float SampleHeight(uint2 p)
{
    // Load expects int3
    return gInHeight.Load(int3(p.xy, 0));
}

// Core outflow computation for a single cell
float4 ComputeOutflow(uint2 p)
{
    uint x = p.x;
    uint y = p.y;

    // Clamp neighbor coordinates to valid range
    uint xm1 = (x == 0)          ? 0         : x - 1;
    uint xp1 = (x + 1 >= Width)  ? Width - 1 : x + 1;
    uint ym1 = (y == 0)          ? 0         : y - 1;
    uint yp1 = (y + 1 >= Height) ? Height-1  : y + 1;

    float hC = SampleHeight(uint2(x,   y));
    float hR = SampleHeight(uint2(xp1, y));   // +X
    float hL = SampleHeight(uint2(xm1, y));   // -X
    float hD = SampleHeight(uint2(x,   yp1)); // +Y (down)
    float hU = SampleHeight(uint2(x,   ym1)); // -Y (up)

    // Clamp parameters to safe ranges.
    // Talus below zero doesn't make sense; Strength beyond [0,1] breaks the "fraction" idea.
    float talus    = max(Talus, 0.0f);
    float strength = saturate(Strength);

    // Positive deltas mean center is higher than neighbor (downhill)
    float dR = max(0.0f, hC - hR - talus);
    float dL = max(0.0f, hC - hL - talus);
    float dD = max(0.0f, hC - hD - talus);
    float dU = max(0.0f, hC - hU - talus);

    float sumD = dR + dL + dD + dU;

    float4 outflow = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // Only move material if there is somewhere to go AND there is material here.
    [branch]
    if (sumD > 0.0f && hC > kEpsilon)
    {
        // total amount to move this step (bounded by Strength and availability)
        float moveTotal = min(hC, strength * sumD);

        // Guard just in case; sumD was checked above. kEpsilon keeps the
        // denominator well-behaved if code changes later.
        float invSum = 1.0f / max(sumD, kEpsilon);

        outflow.x = moveTotal * dR * invSum; // to +X
        outflow.y = moveTotal * dL * invSum; // to -X
        outflow.z = moveTotal * dD * invSum; // to +Y
        outflow.w = moveTotal * dU * invSum; // to -Y
    }

    return outflow;
}

// -----------------------------------------------------------------------------
// Compute shader entry points
// -----------------------------------------------------------------------------

// Primary entry point: use this as /E in DXC and as the Entry Point in VS HLSL.
[numthreads(THERMAL_OUTFLOW_THREAD_GROUP_SIZE_X,
            THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Y,
            THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Z)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Bounds check in case dispatch grid is larger than actual texture.
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    uint2 p = dispatchThreadId.xy;
    float4 outflow = ComputeOutflow(p);
    gOutflow[p] = outflow;
}

// Optional alias for toolchains or old project files that still expect `/E main`.
// If your .vcxproj or custom build step uses /E main, you can leave this here;
// otherwise you can delete it and just use CSMain everywhere.
[numthreads(THERMAL_OUTFLOW_THREAD_GROUP_SIZE_X,
            THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Y,
            THERMAL_OUTFLOW_THREAD_GROUP_SIZE_Z)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    CSMain(dispatchThreadId);
}
