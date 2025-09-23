// Very compact "pipe model" pass: push material down-slope (heightmap-only).
// For more advanced shallow-water, split into multiple UAVs (water, sediment, velocity).
Texture2D<float> InHeight : register(t0);
RWTexture2D<float> OutHeight : register(u0);
cbuffer ErodeCB : register(b0) { float ErodeK; float DepositK; float2 _pad; }

float minN(float c, float n, float k, inout float carry) {
    float d = max(0, c - n);
    float move = k * d;
    carry -= move;
    return n + move;
}

[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID) {
    uint2 dim; InHeight.GetDimensions(dim.x, dim.y);
    uint2 xy = tid.xy; if (xy.x>=dim.x || xy.y>=dim.y) return;
    float c = InHeight[xy];
    float carry = c;

    // 4-neighborhood diffusion downhill (toy hydraulic erosion)
    if (xy.x+1<dim.x) carry = minN(carry, InHeight[xy + uint2(1,0)], ErodeK, carry);
    if (xy.y+1<dim.y) carry = minN(carry, InHeight[xy + uint2(0,1)], ErodeK, carry);
    if (xy.x>0)       carry = minN(carry, InHeight[xy - uint2(1,0)], ErodeK, carry);
    if (xy.y>0)       carry = minN(carry, InHeight[xy - uint2(0,1)], ErodeK, carry);

    // Deposit what's left (preserve mass)
    OutHeight[xy] = lerp(c, carry, DepositK);
}
