// ThermalOutflowCS.hlsl
// Pass 1: Given input height map, compute per-pixel outflow to 4 neighbors.
// Stores outflow as float4: (+X, -X, +Y, -Y)
//
// Expected compile settings (DXC / Visual Studio HLSL):
//   Shader Type   : Compute
//   Shader Model  : cs_6_0
//   Entry Point   : CSMain   (or 'main' if you prefer the alias below)
//
// CBuffer layout must match the CPU side (Width/Height/Talus/Strength).

cbuffer ErodeCB : register(b0)
{
    uint  Width;
    uint  Height;
    float Talus;     // minimum slope before material moves (0..1)
    float Strength;  // max fraction of (sum slopes - talus) to move (0..1)
};

// Input height field
Texture2D<float>      gInHeight : register(t0);

// Output outflow to 4 cardinal neighbors: (+X, -X, +Y, -Y)
RWTexture2D<float4>   gOutflow  : register(u0);

// Optional helper: load height with uint2 coords (slightly nicer callsite)
float SampleHeight(uint2 p)
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
    uint xm1 = (x == 0)          ? 0          : x - 1;
    uint xp1 = (x + 1 >= Width)  ? (Width-1)  : x + 1;
    uint ym1 = (y == 0)          ? 0          : y - 1;
    uint yp1 = (y + 1 >= Height) ? (Height-1) : y + 1;

    float hC = SampleHeight(uint2(x,   y));
    float hR = SampleHeight(uint2(xp1, y));   // +X
    float hL = SampleHeight(uint2(xm1, y));   // -X
    float hD = SampleHeight(uint2(x,   yp1)); // +Y (down)
    float hU = SampleHeight(uint2(x,   ym1)); // -Y (up)

    // Positive deltas mean center is higher than neighbor (downhill)
    float dR = max(0.0f, hC - hR - Talus);
    float dL = max(0.0f, hC - hL - Talus);
    float dD = max(0.0f, hC - hD - Talus);
    float dU = max(0.0f, hC - hU - Talus);

    float sumD = dR + dL + dD + dU;

    float4 outflow = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (sumD > 0.0f && hC > 0.0f)
    {
        // total amount to move this step (bounded by Strength and availability)
        float moveTotal = min(hC, Strength * sumD);

        // Guard just in case; sumD was checked above, but this avoids
        // any risk of divide-by-zero if code changes later.
        float invSum = 1.0f / sumD;

        outflow.x = moveTotal * dR * invSum; // to +X
        outflow.y = moveTotal * dL * invSum; // to -X
        outflow.z = moveTotal * dD * invSum; // to +Y
        outflow.w = moveTotal * dU * invSum; // to -Y
    }

    return outflow;
}

// -----------------------------------------------------------------------------
// Compute shader entry point
// -----------------------------------------------------------------------------

// Primary entry point: **this** is what you should use as /E in DXC
// and as the Entry Point in Visual Studio's HLSL settings.
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    uint2 p = dispatchThreadId.xy;
    float4 outflow = ComputeOutflow(p);
    gOutflow[p] = outflow;
}

// Optional alias for toolchains or old project files that still expect `/E main`.
// If your .vcxproj or custom build step uses /E main, you can leave this here;
// otherwise you can delete it and just use CSMain everywhere.
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    CSMain(dispatchThreadId);
}
