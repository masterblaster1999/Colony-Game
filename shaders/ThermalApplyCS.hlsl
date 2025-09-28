// ThermalApplyCS.hlsl
// Pass 2: Apply outflow to produce new height,
// new = center - sum(outgoing) + sum(incoming from 4 neighbors)

cbuffer ErodeCB : register(b0)
{
    uint  Width;
    uint  Height;
    float Talus;
    float Strength;
};

Texture2D<float>   gInHeight   : register(t0);
Texture2D<float4>  gOutflowTex : register(t1);
RWTexture2D<float> gOutHeight  : register(u0);

[numthreads(16,16,1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height) return;

    uint x = DTid.x;
    uint y = DTid.y;

    uint xm1 = (x == 0) ? 0 : x - 1;
    uint xp1 = (x + 1 >= Width)  ? (Width - 1)  : x + 1;
    uint ym1 = (y == 0) ? 0 : y - 1;
    uint yp1 = (y + 1 >= Height) ? (Height - 1) : y + 1;

    float hC = gInHeight.Load(int3(x, y, 0));
    float4 outC = gOutflowTex.Load(int3(x, y, 0));

    // Incoming: neighbor's outflow directed toward this pixel
    float inFromLeft  = gOutflowTex.Load(int3(xm1, y, 0)).x; // neighbor left flows +X
    float inFromRight = gOutflowTex.Load(int3(xp1, y, 0)).y; // neighbor right flows -X
    float inFromUp    = gOutflowTex.Load(int3(x, ym1, 0)).z; // neighbor up flows +Y
    float inFromDown  = gOutflowTex.Load(int3(x, yp1, 0)).w; // neighbor down flows -Y

    float outgoing = outC.x + outC.y + outC.z + outC.w;
    float incoming = inFromLeft + inFromRight + inFromUp + inFromDown;

    float newH = hC - outgoing + incoming;
    // Clamp to a sane range (avoid negative heights)
    newH = saturate(newH);

    gOutHeight[uint2(x,y)] = newH;
}
