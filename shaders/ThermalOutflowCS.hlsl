// ThermalOutflowCS.hlsl
// Pass 1: Given input height map, compute per-pixel outflow to 4 neighbors.
// Stores outflow as float4: (+X, -X, +Y, -Y)

cbuffer ErodeCB : register(b0)
{
    uint  Width;
    uint  Height;
    float Talus;     // minimum slope before material moves (0..1)
    float Strength;  // max fraction of (sum slopes - talus) to move (0..1)
};

Texture2D<float> gInHeight : register(t0);
RWTexture2D<float4> gOutflow : register(u0);

[numthreads(16,16,1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height) return;

    uint x = DTid.x;
    uint y = DTid.y;

    // Helper to clamp coordinates
    uint xm1 = (x == 0) ? 0 : x - 1;
    uint xp1 = (x + 1 >= Width)  ? (Width - 1)  : x + 1;
    uint ym1 = (y == 0) ? 0 : y - 1;
    uint yp1 = (y + 1 >= Height) ? (Height - 1) : y + 1;

    float hC = gInHeight.Load(int3(x, y, 0));
    float hR = gInHeight.Load(int3(xp1, y, 0));
    float hL = gInHeight.Load(int3(xm1, y, 0));
    float hD = gInHeight.Load(int3(x, yp1, 0)); // +Y (down)
    float hU = gInHeight.Load(int3(x, ym1, 0)); // -Y (up)

    // Positive deltas mean center is higher than neighbor
    float dR = max(0.0, hC - hR - Talus);
    float dL = max(0.0, hC - hL - Talus);
    float dD = max(0.0, hC - hD - Talus);
    float dU = max(0.0, hC - hU - Talus);

    float sumD = dR + dL + dD + dU;

    float4 outflow = float4(0,0,0,0);

    if (sumD > 0.0 && hC > 0.0)
    {
        // total amount to move this step (bounded by Strength and availability)
        float moveTotal = min(hC, Strength * sumD);
        float invSum = 1.0 / sumD;

        outflow.x = moveTotal * dR * invSum; // to +X
        outflow.y = moveTotal * dL * invSum; // to -X
        outflow.z = moveTotal * dD * invSum; // to +Y
        outflow.w = moveTotal * dU * invSum; // to -Y
    }

    gOutflow[uint2(x,y)] = outflow;
}
