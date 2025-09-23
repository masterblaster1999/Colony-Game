// Groupshared-free single pass heightmap generator.
// Dispatch: (ceil(W/8), ceil(H/8), 1)
RWTexture2D<float> OutHeight : register(u0);
cbuffer GenCB : register(b0) { float2 WorldScale; float2 Offset; int Oct; float Lac; float Gain; float Warp; };

[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint2 xy = tid.xy;
    uint2 dim; OutHeight.GetDimensions(dim.x, dim.y);
    if (xy.x>=dim.x || xy.y>=dim.y) return;
    float2 p = (float2(xy)/dim) * WorldScale + Offset;

    // Base: domain-warped fBM + subtle cellular ridge
    float h = DomainWarpedFBM(p);
    float cells = WorleyF1(p*0.5);
    h = h - 0.25*cells; // break uniformity

    OutHeight[xy] = saturate(h);
}
