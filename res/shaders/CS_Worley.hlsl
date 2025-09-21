// Writes a heightmap where each pixel = distance to nearest cell point (F1)
RWTexture2D<float> gOutR32F : register(u0); // use R32F UAV; you can later blit/pack

cbuffer Params : register(b0) {
  uint Width, Height;
  float Frequency;     // cells per UV unit (e.g., 8..64)
  uint   Seed;
};

uint hash2(uint x, uint y, uint seed) {
  uint h=seed; h^=x*0x27d4eb2d; h=(h<<13)|(h>>19); h*=0x85ebca6b;
  h^=y*0x165667b1; h^=(h>>16); h*=0xc2b2ae35; h^=(h>>16); return h;
}

[numthreads(16,16,1)]
void main(uint3 tid:SV_DispatchThreadID)
{
  if(tid.x>=Width || tid.y>=Height) return;
  float2 uv = (float2(tid.xy)+0.5)/float2(Width,Height);
  float2 p = uv * Frequency;
  int xi = (int)floor(p.x), yi = (int)floor(p.y);
  float2 fp = p - float2(xi, yi);
  float dmin = 1e9;

  [unroll] for(int j=-1;j<=1;++j)
  [unroll] for(int i=-1;i<=1;++i){
    uint h = hash2(xi+i, yi+j, Seed);
    float rx = (h & 0xffff) / 65535.0;
    float ry = ((h>>16) & 0xffff) / 65535.0;
    float2 q = float2(i,j) + float2(rx,ry);
    float2 d = fp - q;
    dmin = min(dmin, dot(d,d));
  }
  gOutR32F[tid.xy] = sqrt(dmin); // [0, ~1]
}
