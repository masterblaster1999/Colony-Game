Texture2D<float>   gHeight     : register(t0); // R32F/R16F SRV
Texture1D<float4>  gPalette    : register(t1); // RGBA ramp
SamplerState       gSamp       : register(s0);
RWTexture2D<float4> gOutRGBA   : register(u0);

cbuffer Params : register(b0) { uint Width, Height; float Gain; float Bias; };

[numthreads(16,16,1)]
void main(uint3 tid:SV_DispatchThreadID){
  if(tid.x>=Width || tid.y>=Height) return;
  float2 uv = (float2(tid.xy)+0.5)/float2(Width,Height);
  float h = gHeight.Load(int3(tid.xy,0));
  h = saturate(h * Gain + Bias);
  float4 c = gPalette.SampleLevel(gSamp, h, 0);
  gOutRGBA[tid.xy] = c;
}
