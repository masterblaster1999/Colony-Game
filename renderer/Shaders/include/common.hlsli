#ifndef COLONY_COMMON_HLSLI
#define COLONY_COMMON_HLSLI

// ---- Constants & utilities --------------------------------------------------

static const float PI = 3.14159265359f;

float3 LinearToSRGB(float3 x) {
    // IEC 61966-2-1 approximation
    float3 a = 12.92f * x;
    float3 b = 1.055f * pow(max(x, 0.0f), 1.0f/2.4f) - 0.055f;
    return lerp(a, b, step(0.0031308f.xxx, x));
}

float3 SRGBToLinear(float3 x) {
    float3 a = x / 12.92f;
    float3 b = pow((x + 0.055f) / 1.055f, 2.4f);
    return lerp(a, b, step(0.04045f.xxx, x));
}

// ---- Camera / Object data ---------------------------------------------------

cbuffer CameraCB : register(b0)
{
    float4x4 gWorld;         // object->world
    float4x4 gViewProj;      // world->clip
    float3   gCameraPosWS;
    float    gTime;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 positionCS : SV_Position;
    float3 normalWS   : TEXCOORD1;
    float2 uv         : TEXCOORD0;
};

// ---- Helpers ----------------------------------------------------------------

float4 ToClip(float3 posWS)
{
    return mul(float4(posWS, 1.0f), gViewProj);
}

float3 TransformNormal(float3 n, float4x4 m)
{
    // Assuming orthonormal world (OK for most D3D11 samples)
    return normalize(mul(n, (float3x3)m));
}

#endif // COLONY_COMMON_HLSLI
