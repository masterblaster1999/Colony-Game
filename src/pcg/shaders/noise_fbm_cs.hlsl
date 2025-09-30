// Compute fBm into an R32_FLOAT texture
// Threadgroup = 8x8

RWTexture2D<float> OutTex : register(u0);

cbuffer Params : register(b0)
{
    float2  InvDim;      // 1/width, 1/height
    float   Scale;       // base frequency scale
    int     Octaves;
    float   Lacunarity;
    float   Gain;
    float   Z;           // 3D z slice
    uint    Seed;        // 32-bit seed
};

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 dim;
    OutTex.GetDimensions(dim.x, dim.y);
    if (id.x >= dim.x || id.y >= dim.y) return;

    float2 uv = float2(id.x * InvDim.x, id.y * InvDim.y);

    // hash helpers
    uint hash(uint x){ x ^= x>>16; x*=0x7feb352d; x^=x>>15; x*=0x846ca68b; x^=x>>16; return x; }
    float n2(float2 p, uint s) {
        // simple gradient noise
        uint i = hash(asuint(p.x) ^ (hash(asuint(p.y)) + s));
        float2 g = normalize(float2((i & 0xffu)/255.0-0.5, ((i>>8)&0xffu)/255.0-0.5));
        float2 f = frac(p) - 0.5;
        float2 ip = floor(p);
        // 2x2 quad blend
        float v = 0;
        [unroll] for(int oy=0;oy<2;++oy)
        [unroll] for(int ox=0;ox<2;++ox) {
            float2 pp = f - float2(ox,oy);
            float2 gg = normalize(float2(hash(asuint(ip.x+ox) ^ (hash(asuint(ip.y+oy))+s)) & 1023u,
                                         hash(asuint(ip.y+oy) ^ (hash(asuint(ip.x+ox))+s)) & 1023u) - 512.0) / 512.0;
            float w = smoothstep(0,1,1-abs(pp.x)) * smoothstep(0,1,1-abs(pp.y));
            v += dot(gg, pp) * w;
        }
        return v;
    }

    float amp = 0.5;
    float freq = 1.0;
    float val = 0.0;
    float2 p = uv / Scale;

    [loop]
    for (int o=0;o<Octaves;++o) {
        val += amp * n2(p * freq, Seed + o * 1619u);
        freq *= Lacunarity;
        amp  *= Gain;
    }

    OutTex[id.xy] = val; // approx [-1,1] scaled by amp sum
}
