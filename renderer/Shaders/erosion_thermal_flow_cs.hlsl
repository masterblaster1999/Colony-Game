// Pass 1: compute directional outflows from each texel based on talus angle.
// outflow.x = to +x (right), outflow.y = to -x (left), outflow.z = to -y (up), outflow.w = to +y (down)

cbuffer FlowParams : register(b0)
{
    float talus;
    float carry;   // 0..1 fraction of (slope - talus) moved
    int   width;
    int   height;
};

Texture2D<float>        HeightIn  : register(t0);
RWTexture2D<float4>     FlowOut   : register(u0);

[numthreads(16,16,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)width || id.y >= (uint)height) return;
    int2 p = int2(id.xy);

    float hc = HeightIn[p];

    int2 pL = int2(max(p.x-1,0), p.y);
    int2 pR = int2(min(p.x+1,width-1), p.y);
    int2 pU = int2(p.x, max(p.y-1,0));
    int2 pD = int2(p.x, min(p.y+1,height-1));

    float hL = HeightIn[pL];
    float hR = HeightIn[pR];
    float hU = HeightIn[pU];
    float hD = HeightIn[pD];

    float sL = max(0.0, hc - hL - talus);
    float sR = max(0.0, hc - hR - talus);
    float sU = max(0.0, hc - hU - talus);
    float sD = max(0.0, hc - hD - talus);

    float total = sL + sR + sU + sD + 1e-6;
    float4 outflow = float4(
        carry * sR / total, // to +x
        carry * sL / total, // to -x
        carry * sU / total, // to -y
        carry * sD / total  // to +y
    );
    FlowOut[p] = outflow;
}
