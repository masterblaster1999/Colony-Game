#pragma once
#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>

struct SDFDitherParams {
    // output size
    uint32_t width  = 512;
    uint32_t height = 512;

    // toggles
    bool useBayer = true;
    bool useBlue  = false; // if true, bind blueNoiseSRV

    // UV transform
    float uvScale[2]  = { 1.0f, 1.0f };
    float uvOffset[2] = { 0.0f, 0.0f };

    // circle
    float circleCenter[2] = { 0.5f, 0.5f };
    float circleRadius    = 0.3f;

    // AA width in UV units (typ: 1.0f / height)
    float aaPixel = 1.f / 512.f;

    // rounded box
    float boxCenter[2] = { 0.5f, 0.5f };
    float boxHalf[2]   = { 0.3f, 0.2f };
    float boxRound     = 0.05f;

    // blend between shapes (0 = hard union)
    float smoothK      = 0.03f;

    // colors (linear RGBA)
    float fg[4] = { 1, 1, 1, 1 };
    float bg[4] = { 0, 0, 0, 1 };
};

struct SDFDitherResult {
    Microsoft::WRL::ComPtr<ID3D11Texture2D>            tex; // UAV|SRV
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>   srv;
};

/// Compile-time layout mirror of HLSL cbuffer (16-byte aligned)
struct alignas(16) SDFCB
{
    uint32_t Width, Height, UseBayer, UseBlue;
    float UVScale[2];
    float UVOffset[2];
    float CircleCenter[2];
    float CircleRadius;
    float AAPixel;
    float BoxCenter[2];
    float BoxHalf[2];
    float BoxRound;
    float SmoothK;
    float _Pad0[2];
    float FG[4];
    float BG[4];
};

/// Creates an RGBA8 UAV|SRV texture and runs the compute shader to fill it.
/// If params.useBlue == true, pass a valid blueNoiseSRV (R8_UNORM).
SDFDitherResult RunSDFDitherCS(
    ID3D11Device* device,
    ID3D11DeviceContext* ctx,
    ID3D11ComputeShader* csSdfDither,
    const SDFDitherParams& params,
    ID3D11ShaderResourceView* blueNoiseSRV // can be nullptr if !useBlue
);
