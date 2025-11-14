// Minimal single-pass hydraulic erosion (height + water) for experimentation.
// t0: HeightTex (R32_FLOAT)
// u0: HeightOut (R32_FLOAT)
// u1: Water (R32_FLOAT)
// u2: Sediment (R32_FLOAT)

cbuffer ErosionCB : register(b0)
{
    uint  Width;
    uint  Height;
    float Rain;    // rain per step
    float Evap;    // evaporation per step
    float SedCap;  // sediment capacity factor
    float Kd;      // dissolve coefficient
    float Ks;      // deposit coefficient
    float dt;      // time step
};

Texture2D<float>       HeightTex  : register(t0);
RWTexture2D<float>     HeightOut  : register(u0);
RWTexture2D<float>     Water      : register(u1);
RWTexture2D<float>     Sediment   : register(u2);

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height) return;
    uint2 uv = id.xy;

    float h = HeightTex[uv];
    float w = Water[uv] + Rain * dt;

    // 4-neighborhood heights (clamped)
    float hL = HeightTex[uint2(max(int(uv.x) - 1, 0),            uv.y)];
    float hR = HeightTex[uint2(min(uv.x + 1, Width  - 1),        uv.y)];
    float hU = HeightTex[uint2(uv.x,                            max(int(uv.y) - 1, 0))];
    float hD = HeightTex[uint2(uv.x,                            min(uv.y + 1, Height - 1))];

    float wL = Water[uint2(max(int(uv.x) - 1, 0),            uv.y)];
    float wR = Water[uint2(min(uv.x + 1, Width  - 1),        uv.y)];
    float wU = Water[uint2(uv.x,                            max(int(uv.y) - 1, 0))];
    float wD = Water[uint2(uv.x,                            min(uv.y + 1, Height - 1))];

    float base = h + w;

    float4 neighborHW = float4(
        hL + wL,
        hR + wR,
        hU + wU,
        hD + wD
    );

    // Slope toward neighbors
    float4 diff = float4(base, base, base, base) - neighborHW;

    float4 diffPos = max(diff, 0.0);
    float  sumPos  = diffPos.x + diffPos.y + diffPos.z + diffPos.w;

    float4 flow = (sumPos > 1e-5) ? (diffPos / sumPos) * w : float4(0, 0, 0, 0);

    // Erode/deposit based on total slope
    float slope    = sumPos;
    float capacity = max(0.0f, slope * SedCap);

    float sed   = Sediment[uv];
    float delta = 0.0f;

    if (sed < capacity)
    {
        float erode = min(Kd * (capacity - sed), h);
        h   -= erode;
        sed += erode;
        delta -= erode;
    }
    else
    {
        float deposit = Ks * (sed - capacity);
        h   += deposit;
        sed -= deposit;
        delta += deposit;
    }

    // Local water reduction (we **don’t** push to neighbors in this toy shader)
    w = max(0.0f, w - (flow.x + flow.y + flow.z + flow.w));

    // Evaporation
    w = max(0.0f, w * (1.0f - Evap * dt));

    // Write back
    Water[uv]    = w;
    Sediment[uv] = sed;
    HeightOut[uv]= h;
}
