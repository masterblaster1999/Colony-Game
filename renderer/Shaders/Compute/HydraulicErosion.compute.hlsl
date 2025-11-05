// Minimal single-pass hydraulic erosion (height + water) for experimentation.
// Bindings:
//  - t0: height (read)
//  - u0: heightOut (write)
//  - u1: water (RW)
//  - b0: constants
// Dispatch: [ceil(W/8), ceil(H/8), 1]

cbuffer ErosionCB : register(b0)
{
    uint Width;
    uint Height;
    float Rain;        // rain per step
    float Evap;        // evaporation per step
    float SedCap;      // sediment capacity factor
    float Kd;          // dissolve coefficient
    float Ks;          // deposit coefficient
    float dt;          // time step
};

Texture2D<float> HeightTex : register(t0);
RWTexture2D<float> HeightOut : register(u0);
RWTexture2D<float> Water     : register(u1);
RWTexture2D<float> Sediment  : register(u2);

SamplerState sampLinear : register(s0);

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height) return;

    uint2 uv = id.xy;
    float h = HeightTex[uv];
    float w = Water[uv] + Rain * dt;

    // 4-neighborhood flow based on height+water
    float4 HN;
    HN.x = HeightTex[uint2(max(int(uv.x)-1,0), uv.y)];
    HN.y = HeightTex[uint2(min(uv.x+1,Width-1), uv.y)];
    HN.z = HeightTex[uint2(uv.x, max(int(uv.y)-1,0))];
    HN.w = HeightTex[uint2(uv.x, min(uv.y+1,Height-1))];

    float base = h + w;
    float4 diff = base.xxxy - float4(HN.x + Water[uint2(max(int(uv.x)-1,0), uv.y)],
                                     HN.y + Water[uint2(min(uv.x+1,Width-1), uv.y)],
                                     HN.z + Water[uint2(uv.x, max(int(uv.y)-1,0))],
                                     HN.w + Water[uint2(uv.x, min(uv.y+1,Height-1))]);

    float sumPos = max(diff.x,0) + max(diff.y,0) + max(diff.z,0) + max(diff.w,0);
    float4 flow = (sumPos > 1e-5) ? max(diff, 0) / sumPos * w : float4(0,0,0,0);

    // Erode/deposit
    float slope = sumPos; // crude slope proxy
    float capacity = max(0.0f, slope * SedCap);
    float sed = Sediment[uv];
    float delta = 0.0f;
    if (sed < capacity) {
        float erode = min(Kd * (capacity - sed), h);
        h -= erode; sed += erode;
        delta -= erode;
    } else {
        float deposit = Ks * (sed - capacity);
        h += deposit; sed -= deposit;
        delta += deposit;
    }

    // Water redistribution
    w = w - (flow.x + flow.y + flow.z + flow.w);
    InterlockedAdd(Water[uint2(max(int(uv.x)-1,0), uv.y)], flow.x);
    InterlockedAdd(Water[uint2(min(uv.x+1,Width-1), uv.y)], flow.y);
    InterlockedAdd(Water[uint2(uv.x, max(int(uv.y)-1,0))], flow.z);
    InterlockedAdd(Water[uint2(uv.x, min(uv.y+1,Height-1))], flow.w);

    // Evaporation
    w = max(0.0f, w * (1.0f - Evap * dt));
    Water[uv] = w;
    Sediment[uv] = sed;
    HeightOut[uv] = h;
}
