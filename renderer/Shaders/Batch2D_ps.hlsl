// renderer/Shaders/Batch2D_ps.hlsl
struct PSIn { float4 svpos : SV_POSITION; float4 col : COLOR; };
float4 PSMain(PSIn i) : SV_TARGET { return i.col; }
