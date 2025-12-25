// src/procedural/HeightmapGenerator.cpp
//
// Requires: d3d11.h, d3dcompiler.h, wrl/client.h, DirectXTex (CaptureTexture/SaveToWICFile)
// Link: d3d11.lib, d3dcompiler.lib, windowscodecs.lib (for WIC via DirectXTex)
//
// This module generates a heightmap and corresponding normal map using
// Direct3D 11 compute shaders, then saves both as PNGs via DirectXTex +
// Windows Imaging Component (WIC).

#ifndef _WIN32
#  error "HeightmapGenerator.cpp currently supports only Windows (WIC + D3D11)."
#endif

// -----------------------------------------------------------------------------
// Windows + WIC
// -----------------------------------------------------------------------------
#ifdef __has_include
#  if __has_include("platform/win/WinHeaders.h")
#    include "platform/win/WinHeaders.h"
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <Windows.h>
#  endif
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

// WIC container format GUIDs (GUID_ContainerFormatPng, etc.)
#include <wincodec.h>

// -----------------------------------------------------------------------------
// D3D11 + DirectXTex
// -----------------------------------------------------------------------------
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXTex.h>
#include <string>
#include <stdexcept>
#include <cstdio>   // sprintf_s for ThrowIfFailedImpl diagnostics

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// -----------------------------------------------------------------------------
// Error handling helper: ThrowIfFailed(hr)
// -----------------------------------------------------------------------------
namespace
{
    [[noreturn]] void ThrowIfFailedImpl(HRESULT hr, const char* expr, const char* file, int line)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "HRESULT 0x%08X from %s (%s:%d)",
                      static_cast<unsigned>(hr), expr, file, line);
        throw std::runtime_error(buf);
    }
}

#define ThrowIfFailed(hr_)                                                   \
    do {                                                                     \
        HRESULT _hr = (hr_);                                                 \
        if (FAILED(_hr))                                                     \
            ThrowIfFailedImpl(_hr, #hr_, __FILE__, __LINE__);                \
    } while (0)

// -----------------------------------------------------------------------------
// Constant-buffer layouts for compute shaders
// -----------------------------------------------------------------------------
struct HeightCSParams
{
    UINT  Size[2];          // width, height
    UINT  Seed;
    float Frequency;
    float Lacunarity;
    float Gain;
    UINT  Octaves;
    float ContinentFalloff;
    float HeightPower;
    float _pad[2];          // align to 16 bytes
};

struct NormalCSParams
{
    UINT  Size[2];
    float NormalScale;
    float _pad;
};

// -----------------------------------------------------------------------------
// Simple CS shader compiler helper (D3DCompileFromFile)
// -----------------------------------------------------------------------------
static ComPtr<ID3DBlob> CompileCS(const std::wstring& file, const char* entry)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;

    ThrowIfFailed(D3DCompileFromFile(
        file.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry,
        "cs_5_0",
        flags,
        0,
        &bytecode,
        &errors));

    // If you want to surface HLSL compiler warnings/errors, you can inspect
    // 'errors' here and push to your logging system before returning.
    return bytecode;
}

// -----------------------------------------------------------------------------
// Main entry: generate height + normal maps and save as PNGs
// -----------------------------------------------------------------------------
void GenerateHeightAndNormalPNG(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    UINT                 width,
    UINT                 height,
    UINT                 seed,
    const std::wstring&  outHeightPng,
    const std::wstring&  outNormalPng)
{
    if (!device || !ctx)
        throw std::invalid_argument("GenerateHeightAndNormalPNG: device/ctx must not be null.");

    // 1) Compile compute shaders
    auto csHeightBC = CompileCS(L"renderer/Shaders/CS_GenerateHeight.hlsl", "CS");
    auto csNormalBC = CompileCS(L"renderer/Shaders/CS_HeightToNormal.hlsl", "CS");

    ComPtr<ID3D11ComputeShader> csHeight;
    ComPtr<ID3D11ComputeShader> csNormal;

    ThrowIfFailed(device->CreateComputeShader(
        csHeightBC->GetBufferPointer(),
        csHeightBC->GetBufferSize(),
        nullptr,
        &csHeight));

    ThrowIfFailed(device->CreateComputeShader(
        csNormalBC->GetBufferPointer(),
        csNormalBC->GetBufferSize(),
        nullptr,
        &csNormal));

    // 2) Create height texture (R32_FLOAT UAV + SRV)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = width;
    texDesc.Height             = height;
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = 1;
    texDesc.Format             = DXGI_FORMAT_R32_FLOAT; // typed UAV-friendly
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> heightTex;
    ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, &heightTex));

    // UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
    uavd.Format        = texDesc.Format;
    uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    ComPtr<ID3D11UnorderedAccessView> heightUAV;
    ThrowIfFailed(device->CreateUnorderedAccessView(heightTex.Get(), &uavd, &heightUAV));

    // SRV (for normal pass)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format                    = texDesc.Format;
    srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels       = 1;
    srvd.Texture2D.MostDetailedMip = 0;

    ComPtr<ID3D11ShaderResourceView> heightSRV;
    ThrowIfFailed(device->CreateShaderResourceView(heightTex.Get(), &srvd, &heightSRV));

    // 3) Constant buffer for height generation
    HeightCSParams hparams = {};
    hparams.Size[0]         = width;
    hparams.Size[1]         = height;
    hparams.Seed            = seed;
    hparams.Frequency       = 8.0f;
    hparams.Lacunarity      = 2.0f;
    hparams.Gain            = 0.5f;
    hparams.Octaves         = 7;
    hparams.ContinentFalloff= 0.78f;
    hparams.HeightPower     = 1.15f;

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(HeightCSParams);
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.Usage          = D3D11_USAGE_DEFAULT;
    cbd.CPUAccessFlags = 0;
    cbd.MiscFlags      = 0;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = &hparams;

    ComPtr<ID3D11Buffer> cbHeight;
    ThrowIfFailed(device->CreateBuffer(&cbd, &init, &cbHeight));

    // Dispatch height compute
    ID3D11UnorderedAccessView* uavs[] = { heightUAV.Get() };
    UINT                       initCounts[] = { 0 };

    ctx->CSSetShader(csHeight.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    ID3D11Buffer* cbs[] = { cbHeight.Get() };
    ctx->CSSetConstantBuffers(0, 1, cbs);

    const UINT gx = (width  + 7) / 8;
    const UINT gy = (height + 7) / 8;
    ctx->Dispatch(gx, gy, 1);

    // Unbind UAV
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    UINT                       nullCounts[1] = { 0 };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullCounts);

    // 4) Save height to PNG using DirectXTex (Capture → Convert → Save)
    {
        ScratchImage captured;
        ThrowIfFailed(CaptureTexture(device, ctx, heightTex.Get(), captured)); // GPU → CPU copy
        const Image* src = captured.GetImage(0, 0, 0);

        // Convert R32_FLOAT [0..1] → R8_UNORM for PNG
        ScratchImage converted;
        ThrowIfFailed(Convert(*src, DXGI_FORMAT_R8_UNORM, TEX_FILTER_DEFAULT, 0.0f, converted));

        ThrowIfFailed(SaveToWICFile(
            *converted.GetImage(0, 0, 0),
            WIC_FLAGS_NONE,
            GUID_ContainerFormatPng, // from <wincodec.h>
            outHeightPng.c_str()));
    }

    // 5) Generate normal map on GPU and save
    {
        // Create RGBA32F target for normals (typed UAV)
        D3D11_TEXTURE2D_DESC ndesc = texDesc;
        ndesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

        ComPtr<ID3D11Texture2D> normalTex;
        ThrowIfFailed(device->CreateTexture2D(&ndesc, nullptr, &normalTex));

        D3D11_UNORDERED_ACCESS_VIEW_DESC nuavd = {};
        nuavd.Format        = ndesc.Format;
        nuavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        ComPtr<ID3D11UnorderedAccessView> normalUAV;
        ThrowIfFailed(device->CreateUnorderedAccessView(normalTex.Get(), &nuavd, &normalUAV));

        // CB for normal pass
        NormalCSParams nparams = { { width, height }, 2.0f, 0.0f };

        D3D11_BUFFER_DESC ncbd = {};
        ncbd.ByteWidth      = sizeof(NormalCSParams);
        ncbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        ncbd.Usage          = D3D11_USAGE_DEFAULT;
        ncbd.CPUAccessFlags = 0;
        ncbd.MiscFlags      = 0;

        D3D11_SUBRESOURCE_DATA ninit = {};
        ninit.pSysMem = &nparams;

        ComPtr<ID3D11Buffer> cbNormal;
        ThrowIfFailed(device->CreateBuffer(&ncbd, &ninit, &cbNormal));

        // Bind and dispatch
        ctx->CSSetShader(csNormal.Get(), nullptr, 0);

        ID3D11ShaderResourceView* srvs[] = { heightSRV.Get() };
        ctx->CSSetShaderResources(0, 1, srvs);

        ID3D11UnorderedAccessView* nuavs[] = { normalUAV.Get() };
        ctx->CSSetUnorderedAccessViews(0, 1, nuavs, initCounts);

        ID3D11Buffer* ncbs[] = { cbNormal.Get() };
        ctx->CSSetConstantBuffers(0, 1, ncbs);

        ctx->Dispatch(gx, gy, 1);

        // Unbind resources
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullCounts);

        // Save normal as PNG (convert RGBA32F → 8‑bit RGBA)
        ScratchImage capturedN;
        ThrowIfFailed(CaptureTexture(device, ctx, normalTex.Get(), capturedN));
        const Image* srcN = capturedN.GetImage(0, 0, 0);

        ScratchImage convertedN;
        ThrowIfFailed(Convert(*srcN, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, 0.0f, convertedN));

        ThrowIfFailed(SaveToWICFile(
            *convertedN.GetImage(0, 0, 0),
            WIC_FLAGS_NONE,
            GUID_ContainerFormatPng, // from <wincodec.h>
            outNormalPng.c_str()));
    }
}
