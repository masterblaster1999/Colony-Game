Texture2D<float>    gHeight : register(t0);
RWTexture2D<float4> gOut    : register(u0);

cbuffer Params : register(b0) { uint Width, Height; float Strength; };

[numthreads(16,16,1)]
void main(uint3 tid:SV_DispatchThreadID){
  if(tid.x>=Width || tid.y>=Height) return;
  int2 p = tid.xy;
  float hL = gHeight.Load(int3(max(p-int2(1,0),0),0));
  float hR = gHeight.Load(int3(min(p+int2(1,0), int2(Width-1,Height-1)),0));
  float hD = gHeight.Load(int3(max(p-int2(0,1),0),0));
  float hU = gHeight.Load(int3(min(p+int2(0,1), int2(Width-1,Height-1)),0));
  float2 g = float2(hR - hL, hU - hD) * Strength;
  float3 n = normalize(float3(-g.x, -g.y, 1.0));
  gOut[p] = float4(n*0.5 + 0.5, 1.0);
}
