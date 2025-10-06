#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace render
{
    struct Texture2D
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        uint32_t width = 0, height = 0;
    };

    Texture2D CreateRWTexture2D(ID3D11Device* dev,
                                uint32_t width, uint32_t height,
                                DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT);

    Microsoft::WRL::ComPtr<ID3D11Buffer>
    CreateConstantBuffer(ID3D11Device* dev, size_t byteSize);
}
