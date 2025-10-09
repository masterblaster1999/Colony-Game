// Pass 2: apply outflows to current height (ping-pong A->B).
cbuffer ApplyParams : register(b0)
{
    int width;
    int height;
    float2 _pad;
};

Texture2D<float>     HeightIn  : register(t0);
Texture2D<float4>    FlowIn    : register(t1);
RWTexture2D<float>   HeightOut : register(u0);

[numthreads(16,16,1)]
void CSMain(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= (uint)width || id.y >= (uint)height) return;
    int2 p = int2(id.xy);

    float4 of = FlowIn[p];

    int2 pL = int2(max(p.x-1,0), p.y);
    int2 pR = int2(min(p.x+1,width-1), p.y);
    int2 pU = int2(p.x, max(p.y-1,0));
    int2 pD = int2(p.x, min(p.y+1,height-1));

    float incoming =
        FlowIn[pL].x +      // left neighbor sends right
        FlowIn[pR].y +      // right neighbor sends left
        FlowIn[pU].w +      // up neighbor sends down
        FlowIn[pD].z;       // down neighbor sends up

    float outgoing = of.x + of.y + of.z + of.w;

    float h = HeightIn[p] - outgoing + incoming;
    HeightOut[p] = h;
}
